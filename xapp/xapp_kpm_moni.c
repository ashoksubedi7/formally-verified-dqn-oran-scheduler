/*
 * Licensed to the OpenAirInterface (OAI) Software Alliance under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The OpenAirInterface Software Alliance licenses this file to You under
 * the OAI Public License, Version 1.1  (the "License"); you may not use this file
 * except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.openairinterface.org/?page_id=698
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BAS
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *-------------------------------------------------------------------------------
 * For more information about the OpenAirInterface (OAI) Software Alliance:
 *      contact@openairinterface.org
 */

#include "../../../../src/xApp/e42_xapp_api.h"
#include "../../../../src/util/alg_ds/alg/defer.h"
#include "../../../../src/util/time_now_us.h"
#include "../../../../src/util/alg_ds/ds/lock_guard/lock_guard.h"
#include "../../../../src/util/e.h"
#include "../../../../src/sm/slice_sm/slice_sm_id.h"

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>

#include "onnxruntime_c_api.h"
#include <openssl/evp.h>
#include <string.h>

static
uint64_t const period_ms = 1000;

static
pthread_mutex_t mtx;

/*
 * Current prototype has one OAI gNB/E2 node.
 *
 * The pointer refers to nodes.n[0].id owned by main(), whose lifetime
 * covers the KPM callbacks.
 *
 * This is control-plane state only and is independent of RFsim/USRP.
 */
static global_e2_node_id_t* dqn_ctrl_node_id = NULL;

/*
 * Last successfully transmitted DQN action.
 *
 * -1 means no policy has been sent yet.
 * Avoids redundant E2 control messages when the DQN decision is unchanged.
 */
static int dqn_last_sent_action = -1;

/* Exact formally verified ONNX artifact. */
static const char* const DQN_MODEL_PATH =
    "/home/rpaudyal/openairinterface5g/formal-verification/"
    "py5chesim/models/formal_dqn_v2_candidate_16000.onnx";

static const char* const DQN_MODEL_SHA256 =
    "699c2d326b8c86f81d86929fcf28e3a00086687e0391c6a1bd4119b92001103e";

static const OrtApi* dqn_ort = NULL;
static OrtEnv* dqn_ort_env = NULL;
static OrtSessionOptions* dqn_ort_session_options = NULL;
static OrtSession* dqn_ort_session = NULL;
static OrtMemoryInfo* dqn_ort_memory_info = NULL;

/*
 * Exact Py5cheSim action ordering:
 * [w_eMBB, w_URLLC, w_mMTC]
 */
static const int dqn_action_weights[22][3] = {
    {  0,   0,   0},
    {  0,   0, 100},
    {  0,  20,  80},
    {  0,  40,  60},
    {  0,  60,  40},
    {  0,  80,  20},
    {  0, 100,   0},
    { 20,  80,   0},
    { 40,  60,   0},
    { 60,  40,   0},
    { 80,  20,   0},
    {100,   0,   0},
    { 80,   0,  20},
    { 60,   0,  40},
    { 40,   0,  60},
    { 20,   0,  80},
    { 60,  20,  20},
    { 20,  60,  20},
    { 20,  20,  60},
    { 40,  40,  20},
    { 40,  20,  40},
    { 20,  40,  40}
};


/*
 * Send one DQN inter-slice policy through FlexRIC Slice-SM.
 *
 * DQN ordering:
 *
 *   [eMBB, URLLC, mMTC]
 *
 * Slice-SM transport mapping:
 *
 *   id=0 -> eMBB
 *   id=1 -> URLLC
 *   id=2 -> mMTC
 *
 * pct_reserved is only the wire representation:
 *
 *   DQN weight 20 -> 0.20
 *
 * The weights are NOT literal PRB counts.
 */
static
bool send_dqn_slice_policy(global_e2_node_id_t* node,
                           int w_embb,
                           int w_urllc,
                           int w_mmtc)
{
  assert(node != NULL);

  int const weights[3] = {
      w_embb,
      w_urllc,
      w_mmtc
  };

  slice_ctrl_req_data_t ctrl = {0};

  ctrl.msg.type = SLICE_CTRL_SM_V0_ADD;

  ul_dl_slice_conf_t* dl =
      &ctrl.msg.u.add_mod_slice.dl;

  /*
   * Keep scheduler name conventional.
   * The OAI DQN handler currently uses the NVS capacity entries.
   */
  char const* sched_name = "PF";

  dl->len_sched_name = strlen(sched_name);
  dl->sched_name = calloc(dl->len_sched_name, sizeof(char));

  if (dl->sched_name == NULL)
    return false;

  memcpy(dl->sched_name,
         sched_name,
         dl->len_sched_name);

  dl->len_slices = 3;
  dl->slices = calloc(dl->len_slices, sizeof(fr_slice_t));

  if (dl->slices == NULL) {
    free_slice_ctrl_msg(&ctrl.msg);
    return false;
  }

  char const* labels[3] = {
      "eMBB",
      "URLLC",
      "mMTC"
  };

  for (uint32_t i = 0; i < dl->len_slices; ++i) {

    fr_slice_t* slice = &dl->slices[i];

    slice->id = i;

    slice->len_label = strlen(labels[i]);
    slice->label =
        calloc(slice->len_label, sizeof(char));

    if (slice->label == NULL) {
      free_slice_ctrl_msg(&ctrl.msg);
      return false;
    }

    memcpy(slice->label,
           labels[i],
           slice->len_label);

    slice->len_sched = strlen(sched_name);
    slice->sched =
        calloc(slice->len_sched, sizeof(char));

    if (slice->sched == NULL) {
      free_slice_ctrl_msg(&ctrl.msg);
      return false;
    }

    memcpy(slice->sched,
           sched_name,
           slice->len_sched);

    slice->params.type =
        SLICE_ALG_SM_V0_NVS;

    slice->params.u.nvs.conf =
        SLICE_SM_NVS_V0_CAPACITY;

    slice->params.u.nvs.u.capacity.u.pct_reserved =
        (float)weights[i] / 100.0f;
  }

  /*
   * This project currently controls DL slicing only.
   */
  ctrl.msg.u.add_mod_slice.ul.len_slices = 0;

  printf("[DQN E2 CONTROL] sending weights=[%d,%d,%d]\n",
         w_embb,
         w_urllc,
         w_mmtc);

  sm_ans_xapp_t const ans =
      control_sm_xapp_api(node,
                          SM_SLICE_ID,
                          &ctrl);

  free_slice_ctrl_msg(&ctrl.msg);

  if (!ans.success) {
    fprintf(stderr,
            "[DQN E2 CONTROL] Slice-SM control failed\n");
    return false;
  }

  printf("[DQN E2 CONTROL] Slice-SM control sent successfully\n");

  return true;
}


static
bool dqn_sha256_file(const char* path, char out_hex[65])
{
  FILE* fp = fopen(path, "rb");

  if (fp == NULL) {
    fprintf(stderr,
            "[DQN MODEL] Cannot open model for SHA256: %s\n",
            path);
    return false;
  }

  EVP_MD_CTX* ctx = EVP_MD_CTX_new();

  if (ctx == NULL) {
    fclose(fp);
    fprintf(stderr,
            "[DQN MODEL] EVP_MD_CTX_new failed\n");
    return false;
  }

  bool ok = true;

  if (EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) != 1)
    ok = false;

  unsigned char buffer[8192];

  while (ok) {

    size_t const n =
        fread(buffer, 1, sizeof(buffer), fp);

    if (n > 0) {
      if (EVP_DigestUpdate(ctx, buffer, n) != 1) {
        ok = false;
        break;
      }
    }

    if (n < sizeof(buffer)) {

      if (ferror(fp))
        ok = false;

      break;
    }
  }

  unsigned char digest[EVP_MAX_MD_SIZE];
  unsigned int digest_len = 0;

  if (ok) {
    if (EVP_DigestFinal_ex(
            ctx,
            digest,
            &digest_len) != 1) {
      ok = false;
    }
  }

  EVP_MD_CTX_free(ctx);
  fclose(fp);

  if (!ok)
    return false;

  if (digest_len != 32) {
    fprintf(stderr,
            "[DQN MODEL] Unexpected SHA256 length: %u\n",
            digest_len);
    return false;
  }

  static const char hex[] =
      "0123456789abcdef";

  for (unsigned int i = 0; i < digest_len; ++i) {

    out_hex[2 * i] =
        hex[(digest[i] >> 4) & 0x0f];

    out_hex[2 * i + 1] =
        hex[digest[i] & 0x0f];
  }

  out_hex[64] = '\0';

  return true;
}


static
void dqn_verify_model_hash(void)
{
  char actual_sha256[65] = {0};

  if (!dqn_sha256_file(
          DQN_MODEL_PATH,
          actual_sha256)) {

    fprintf(stderr,
            "[DQN MODEL] SHA256 computation failed. "
            "Refusing to load model.\n");

    exit(EXIT_FAILURE);
  }

  printf("[DQN MODEL] Actual SHA256   = %s\n",
         actual_sha256);

  printf("[DQN MODEL] Expected SHA256 = %s\n",
         DQN_MODEL_SHA256);

  if (strcmp(
          actual_sha256,
          DQN_MODEL_SHA256) != 0) {

    fprintf(stderr,
            "[DQN MODEL] SHA256 MISMATCH. "
            "Refusing to load unverified model.\n");

    exit(EXIT_FAILURE);
  }

  printf("[DQN MODEL] SHA256 VERIFIED\n");
}


static
void dqn_ort_check(OrtStatus* status, const char* where)
{
  if (status == NULL)
    return;

  fprintf(stderr,
          "[DQN ONNX ERROR] %s: %s\n",
          where,
          dqn_ort->GetErrorMessage(status));

  dqn_ort->ReleaseStatus(status);
  exit(EXIT_FAILURE);
}

static
void dqn_ort_init(void)
{
  /*
   * Fail closed unless this is exactly the ONNX artifact
   * that was formally verified.
   */
  dqn_verify_model_hash();

  const OrtApiBase* base = OrtGetApiBase();
  assert(base != NULL);

  dqn_ort = base->GetApi(ORT_API_VERSION);
  assert(dqn_ort != NULL);

  dqn_ort_check(
      dqn_ort->CreateEnv(
          ORT_LOGGING_LEVEL_WARNING,
          "formal_dqn_xapp",
          &dqn_ort_env),
      "CreateEnv");

  dqn_ort_check(
      dqn_ort->CreateSessionOptions(
          &dqn_ort_session_options),
      "CreateSessionOptions");

  dqn_ort_check(
      dqn_ort->CreateSession(
          dqn_ort_env,
          DQN_MODEL_PATH,
          dqn_ort_session_options,
          &dqn_ort_session),
      "CreateSession");

  dqn_ort_check(
      dqn_ort->CreateCpuMemoryInfo(
          OrtArenaAllocator,
          OrtMemTypeDefault,
          &dqn_ort_memory_info),
      "CreateCpuMemoryInfo");

  printf("[DQN ONNX] Runtime version = %s\n",
         base->GetVersionString());

  printf("[DQN ONNX] Model = %s\n",
         DQN_MODEL_PATH);

  printf("[DQN ONNX] Expected SHA256 = %s\n",
         DQN_MODEL_SHA256);
}

static
void dqn_ort_shutdown(void)
{
  if (dqn_ort == NULL)
    return;

  if (dqn_ort_memory_info != NULL)
    dqn_ort->ReleaseMemoryInfo(dqn_ort_memory_info);

  if (dqn_ort_session != NULL)
    dqn_ort->ReleaseSession(dqn_ort_session);

  if (dqn_ort_session_options != NULL)
    dqn_ort->ReleaseSessionOptions(dqn_ort_session_options);

  if (dqn_ort_env != NULL)
    dqn_ort->ReleaseEnv(dqn_ort_env);

  dqn_ort_memory_info = NULL;
  dqn_ort_session = NULL;
  dqn_ort_session_options = NULL;
  dqn_ort_env = NULL;
}

static
int dqn_ort_infer(const float state[3],
                  float q_out[22])
{
  assert(dqn_ort_session != NULL);
  assert(dqn_ort_memory_info != NULL);

  float input_data[3] = {
      state[0],
      state[1],
      state[2]
  };

  int64_t input_shape[2] = {1, 3};

  const char* input_names[] = {"dense_input"};
  const char* output_names[] = {"dense_2"};

  OrtValue* input_tensor = NULL;
  OrtValue* output_tensor = NULL;

  dqn_ort_check(
      dqn_ort->CreateTensorWithDataAsOrtValue(
          dqn_ort_memory_info,
          input_data,
          sizeof(input_data),
          input_shape,
          2,
          ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
          &input_tensor),
      "CreateTensorWithDataAsOrtValue");

  dqn_ort_check(
      dqn_ort->Run(
          dqn_ort_session,
          NULL,
          input_names,
          (const OrtValue* const*)&input_tensor,
          1,
          output_names,
          1,
          &output_tensor),
      "Run");

  float* q = NULL;

  dqn_ort_check(
      dqn_ort->GetTensorMutableData(
          output_tensor,
          (void**)&q),
      "GetTensorMutableData");

  int best_action = 0;

  for (int i = 0; i < 22; ++i) {
    q_out[i] = q[i];

    /* Preserve numpy.argmax first-maximum behavior. */
    if (q[i] > q[best_action])
      best_action = i;
  }

  dqn_ort->ReleaseValue(output_tensor);
  dqn_ort->ReleaseValue(input_tensor);

  return best_action;
}


static
void log_gnb_ue_id(ue_id_e2sm_t ue_id)
{
  if (ue_id.gnb.gnb_cu_ue_f1ap_lst != NULL) {
    for (size_t i = 0; i < ue_id.gnb.gnb_cu_ue_f1ap_lst_len; i++) {
      printf("UE ID type = gNB-CU, gnb_cu_ue_f1ap = %u\n", ue_id.gnb.gnb_cu_ue_f1ap_lst[i]);
    }
  } else {
    printf("UE ID type = gNB, amf_ue_ngap_id = %lu\n", ue_id.gnb.amf_ue_ngap_id);
  }
  if (ue_id.gnb.ran_ue_id != NULL) {
    printf("ran_ue_id = %lx\n", *ue_id.gnb.ran_ue_id); // RAN UE NGAP ID
  }
}

static
void log_du_ue_id(ue_id_e2sm_t ue_id)
{
  printf("UE ID type = gNB-DU, gnb_cu_ue_f1ap = %u\n", ue_id.gnb_du.gnb_cu_ue_f1ap);
  if (ue_id.gnb_du.ran_ue_id != NULL) {
    printf("ran_ue_id = %lx\n", *ue_id.gnb_du.ran_ue_id); // RAN UE NGAP ID
  }
}

static
void log_cuup_ue_id(ue_id_e2sm_t ue_id)
{
  printf("UE ID type = gNB-CU-UP, gnb_cu_cp_ue_e1ap = %u\n", ue_id.gnb_cu_up.gnb_cu_cp_ue_e1ap);
  if (ue_id.gnb_cu_up.ran_ue_id != NULL) {
    printf("ran_ue_id = %lx\n", *ue_id.gnb_cu_up.ran_ue_id); // RAN UE NGAP ID
  }
}

typedef void (*log_ue_id)(ue_id_e2sm_t ue_id);

static
log_ue_id log_ue_id_e2sm[END_UE_ID_E2SM] = {
    log_gnb_ue_id, // common for gNB-mono, CU and CU-CP
    log_du_ue_id,
    log_cuup_ue_id,
    NULL,
    NULL,
    NULL,
    NULL,
};

static
void log_int_value(byte_array_t name, meas_record_lst_t meas_record)
{
  if (cmp_str_ba("RRU.PrbTotDl", name) == 0) {
    printf("RRU.PrbTotDl = %d [PRBs]\n", meas_record.int_val);
  } else if (cmp_str_ba("RRU.PrbTotUl", name) == 0) {
    printf("RRU.PrbTotUl = %d [PRBs]\n", meas_record.int_val);
  } else if (cmp_str_ba("DRB.PdcpSduVolumeDL", name) == 0) {
    printf("DRB.PdcpSduVolumeDL = %d [kb]\n", meas_record.int_val);
  } else if (cmp_str_ba("DRB.PdcpSduVolumeUL", name) == 0) {
    printf("DRB.PdcpSduVolumeUL = %d [kb]\n", meas_record.int_val);
  } else if (cmp_str_ba("OAI.RlcTxQueuePktsDl", name) == 0) {
    printf("OAI.RlcTxQueuePktsDl = %d [queued SDUs]\n", meas_record.int_val);
  } else {
    printf("Measurement Name not yet supported\n");
  }
}

static
void log_real_value(byte_array_t name, meas_record_lst_t meas_record)
{
  if (cmp_str_ba("DRB.RlcSduDelayDl", name) == 0) {
    printf("DRB.RlcSduDelayDl = %.2f [μs]\n", meas_record.real_val);
  } else if (cmp_str_ba("DRB.UEThpDl", name) == 0) {
    printf("DRB.UEThpDl = %.2f [kbps]\n", meas_record.real_val);
  } else if (cmp_str_ba("DRB.UEThpUl", name) == 0) {
    printf("DRB.UEThpUl = %.2f [kbps]\n", meas_record.real_val);
  } else {
    printf("Measurement Name not yet supported\n");
  }
}

typedef void (*log_meas_value)(byte_array_t name, meas_record_lst_t meas_record);

static
log_meas_value get_meas_value[END_MEAS_VALUE] = {
    log_int_value,
    log_real_value,
    NULL,
};

static
void match_meas_name_type(meas_type_t meas_type, meas_record_lst_t meas_record)
{
  // Get the value of the Measurement
  get_meas_value[meas_record.value](meas_type.name, meas_record);
}

static
void match_id_meas_type(meas_type_t meas_type, meas_record_lst_t meas_record)
{
  (void)meas_type;
  (void)meas_record;
  assert(false && "ID Measurement Type not yet supported");
}

typedef void (*check_meas_type)(meas_type_t meas_type, meas_record_lst_t meas_record);

static
check_meas_type match_meas_type[END_MEAS_TYPE] = {
    match_meas_name_type,
    match_id_meas_type,
};

static
void log_kpm_measurements(kpm_ind_msg_format_1_t const* msg_frm_1)
{
  assert(msg_frm_1->meas_info_lst_len > 0 && "Cannot correctly print measurements");

  // UE Measurements per granularity period
  for (size_t j = 0; j < msg_frm_1->meas_data_lst_len; j++) {
    meas_data_lst_t const data_item = msg_frm_1->meas_data_lst[j];

    for (size_t z = 0; z < data_item.meas_record_len; z++) {
      meas_type_t const meas_type = msg_frm_1->meas_info_lst[z].meas_type;
      meas_record_lst_t const record_item = data_item.meas_record_lst[z];

      match_meas_type[meas_type.type](meas_type, record_item);

      if (data_item.incomplete_flag && *data_item.incomplete_flag == TRUE_ENUM_VALUE)
        printf("Measurement Record not reliable");
    }
  }

}

/*
 * DQN state ordering must remain:
 *
 *   [0] eMBB
 *   [1] URLLC
 *   [2] mMTC
 *
 * These are raw queued original RLC SDUs aggregated over all
 * UEs belonging to each exact S-NSSAI subscription.
 */
enum {
  DQN_SLICE_EMBB = 0,
  DQN_SLICE_URLLC = 1,
  DQN_SLICE_MMTC = 2,
  DQN_NUM_SLICES = 3
};

static uint64_t dqn_state_raw[DQN_NUM_SLICES] = {0, 0, 0};
static uint32_t dqn_state_clamped[DQN_NUM_SLICES] = {0, 0, 0};
static float dqn_state_norm[DQN_NUM_SLICES] = {0.0f, 0.0f, 0.0f};

/* Freshness tracking for the independently arriving slice reports. */
static bool dqn_state_seen[DQN_NUM_SLICES] = {false, false, false};
static int64_t dqn_state_last_update_us[DQN_NUM_SLICES] = {0, 0, 0};

/* Require one new report from every slice before each inference. */
static bool dqn_state_updated[DQN_NUM_SLICES] = {false, false, false};

static const char* dqn_slice_name[DQN_NUM_SLICES] = {
    "eMBB",
    "URLLC",
    "mMTC"
};

/*
 * Extract the latest OAI.RlcTxQueuePktsDl value for one UE
 * from an Indication Message Format 1.
 *
 * The current DQN KPM subscription requests only this metric.
 */
static
bool get_dqn_queue_value(kpm_ind_msg_format_1_t const* msg_frm_1,
                         uint64_t* queue_value)
{
  assert(msg_frm_1 != NULL);
  assert(queue_value != NULL);

  bool found = false;
  uint64_t latest = 0;

  for (size_t j = 0; j < msg_frm_1->meas_data_lst_len; ++j) {

    meas_data_lst_t const data_item =
        msg_frm_1->meas_data_lst[j];

    for (size_t z = 0; z < data_item.meas_record_len; ++z) {

      meas_type_t const meas_type =
          msg_frm_1->meas_info_lst[z].meas_type;

      meas_record_lst_t const record_item =
          data_item.meas_record_lst[z];

      if (meas_type.type != NAME_MEAS_TYPE)
        continue;

      if (cmp_str_ba("OAI.RlcTxQueuePktsDl",
                     meas_type.name) != 0)
        continue;

      assert(record_item.value == INTEGER_MEAS_VALUE);

      latest = (uint64_t)record_item.int_val;
      found = true;
    }
  }

  if (found)
    *queue_value = latest;

  return found;
}


static
void sm_cb_kpm_slice(sm_ag_if_rd_t const* rd,
                     size_t slice_idx)
{
  assert(rd != NULL);
  assert(slice_idx < DQN_NUM_SLICES);

  assert(rd->type == INDICATION_MSG_AGENT_IF_ANS_V0);
  assert(rd->ind.type == KPM_STATS_V3_0);

  kpm_ind_data_t const* ind = &rd->ind.kpm.ind;

  kpm_ric_ind_hdr_format_1_t const* hdr_frm_1 =
      &ind->hdr.kpm_ric_ind_hdr_format_1;

  kpm_ind_msg_format_3_t const* msg_frm_3 =
      &ind->msg.frm_3;

  int64_t const now = time_now_us();

  static uint64_t counter = 1;

  {
    lock_guard(&mtx);

    printf("\n%7llu KPM [%s] ind_msg latency = %ld [μs]\n",
           (unsigned long long)counter,
           dqn_slice_name[slice_idx],
           now - hdr_frm_1->collectStartTime);

    uint64_t slice_total = 0;

    /*
     * One exact S-NSSAI subscription may contain more than one UE.
     * Sum all UE queue occupancies to reproduce the per-slice
     * packet-load state expected by the DQN.
     */
    for (size_t i = 0;
         i < msg_frm_3->ue_meas_report_lst_len;
         ++i) {

      ue_id_e2sm_t const ue_id_e2sm =
          msg_frm_3->meas_report_per_ue[i].ue_meas_report_lst;

      ue_id_e2sm_e const type = ue_id_e2sm.type;

      log_ue_id_e2sm[type](ue_id_e2sm);

      kpm_ind_msg_format_1_t const* ue_msg =
          &msg_frm_3->meas_report_per_ue[i].ind_msg_format_1;

      uint64_t ue_queue = 0;

      bool const found =
          get_dqn_queue_value(ue_msg, &ue_queue);

      assert(found &&
             "OAI.RlcTxQueuePktsDl missing from DQN KPM indication");

      slice_total += ue_queue;

      printf("OAI.RlcTxQueuePktsDl = %llu [queued SDUs]\n",
             (unsigned long long)ue_queue);
    }

    /*
     * Deployment guard required by the verified DQN domain:
     *
     * raw load -> clamp [0,500] -> divide by 500
     */
    dqn_state_raw[slice_idx] = slice_total;

    dqn_state_clamped[slice_idx] =
        slice_total > 500 ? 500 : (uint32_t)slice_total;

    dqn_state_norm[slice_idx] =
        (float)dqn_state_clamped[slice_idx] / 500.0f;

    dqn_state_seen[slice_idx] = true;
    dqn_state_last_update_us[slice_idx] = now;
    dqn_state_updated[slice_idx] = true;

    printf("[DQN STATE] %s: raw=%llu clamped=%u normalized=%.3f\n",
           dqn_slice_name[slice_idx],
           (unsigned long long)dqn_state_raw[slice_idx],
           dqn_state_clamped[slice_idx],
           dqn_state_norm[slice_idx]);

    printf("[DQN VECTOR] raw=[%llu,%llu,%llu] "
           "clamped=[%u,%u,%u] "
           "normalized=[%.3f,%.3f,%.3f]\n",
           (unsigned long long)dqn_state_raw[0],
           (unsigned long long)dqn_state_raw[1],
           (unsigned long long)dqn_state_raw[2],
           dqn_state_clamped[0],
           dqn_state_clamped[1],
           dqn_state_clamped[2],
           dqn_state_norm[0],
           dqn_state_norm[1],
           dqn_state_norm[2]);

    /*
     * Only allow DQN inference when every slice has been observed
     * and every component is no older than two KPM reporting periods.
     */
    int64_t const freshness_us =
        (int64_t)(2ULL * period_ms * 1000ULL);

    bool dqn_ready = true;
    long long age_ms[DQN_NUM_SLICES] = {-1, -1, -1};

    for (size_t s = 0; s < DQN_NUM_SLICES; ++s) {

      if (!dqn_state_seen[s]) {
        dqn_ready = false;
        continue;
      }

      int64_t const age_us =
          now - dqn_state_last_update_us[s];

      age_ms[s] = (long long)(age_us / 1000);

      if (age_us < 0 || age_us > freshness_us)
        dqn_ready = false;
    }

    printf("[DQN FRESHNESS] seen=[%d,%d,%d] "
           "age_ms=[%lld,%lld,%lld] threshold_ms=%llu\n",
           dqn_state_seen[0],
           dqn_state_seen[1],
           dqn_state_seen[2],
           age_ms[0],
           age_ms[1],
           age_ms[2],
           (unsigned long long)(2ULL * period_ms));

    printf("[DQN READY] %s\n",
           dqn_ready ? "YES" : "NO");

    bool const complete_new_vector =
        dqn_state_updated[0] &&
        dqn_state_updated[1] &&
        dqn_state_updated[2];

    if (dqn_ready && complete_new_vector) {

      float q_values[22] = {0};

      int const action =
          dqn_ort_infer(dqn_state_norm, q_values);

      assert(action >= 0 && action < 22);

      int const w_embb =
          dqn_action_weights[action][0];

      int const w_urllc =
          dqn_action_weights[action][1];

      int const w_mmtc =
          dqn_action_weights[action][2];

      printf("[DQN INFERENCE] input=[%.6f,%.6f,%.6f]\n",
             dqn_state_norm[0],
             dqn_state_norm[1],
             dqn_state_norm[2]);

      printf("[DQN INFERENCE] action=%d maxQ=%.9f "
             "weights=[%d,%d,%d]\n",
             action,
             q_values[action],
             w_embb,
             w_urllc,
             w_mmtc);

      /*
       * Runtime assertion corresponding to the formally verified
       * URLLC-active property.
       *
       * IMPORTANT:
       * These values are scheduler weights, NOT literal PRB counts.
       */
      bool const urllc_active =
          dqn_state_raw[DQN_SLICE_URLLC] >= 1;

      bool const runtime_safe =
          !urllc_active || w_urllc >= 20;

      printf("[DQN SAFETY] URLLC_active=%s "
             "w_URLLC=%d result=%s\n",
             urllc_active ? "YES" : "NO",
             w_urllc,
             runtime_safe ? "PASS" : "FAIL");

      if (!runtime_safe) {
        fprintf(stderr,
                "[DQN SAFETY] Unsafe action rejected: "
                "action=%d w_URLLC=%d\n",
                action,
                w_urllc);
      } else {

        /*
         * Only transmit when the verified DQN policy changes.
         *
         * MAC scheduling continues independently using the most recently
         * installed policy between Near-RT control updates.
         */
        if (action != dqn_last_sent_action) {

          if (dqn_ctrl_node_id == NULL) {

            fprintf(stderr,
                    "[DQN E2 CONTROL] control node unavailable\n");

          } else if (send_dqn_slice_policy(dqn_ctrl_node_id,
                                           w_embb,
                                           w_urllc,
                                           w_mmtc)) {

            dqn_last_sent_action = action;

            printf("[DQN E2 CONTROL] installed action=%d "
                   "weights=[%d,%d,%d]\n",
                   action,
                   w_embb,
                   w_urllc,
                   w_mmtc);

          }

        } else {

          printf("[DQN E2 CONTROL] action=%d unchanged; "
                 "control not resent\n",
                 action);
        }
      }

      /*
       * A decision epoch is complete.
       * Require a new report from all three subscriptions
       * before another inference.
       */
      dqn_state_updated[0] = false;
      dqn_state_updated[1] = false;
      dqn_state_updated[2] = false;
    }

    counter++;
  }
}


/*
 * Separate callbacks preserve the identity of the exact
 * S-NSSAI subscription.
 */
static
void sm_cb_kpm_embb(sm_ag_if_rd_t const* rd)
{
  sm_cb_kpm_slice(rd, DQN_SLICE_EMBB);
}

static
void sm_cb_kpm_urllc(sm_ag_if_rd_t const* rd)
{
  sm_cb_kpm_slice(rd, DQN_SLICE_URLLC);
}

static
void sm_cb_kpm_mmtc(sm_ag_if_rd_t const* rd)
{
  sm_cb_kpm_slice(rd, DQN_SLICE_MMTC);
}

static sm_cb const kpm_slice_callbacks[DQN_NUM_SLICES] = {
    sm_cb_kpm_embb,
    sm_cb_kpm_urllc,
    sm_cb_kpm_mmtc
};

static
test_info_lst_t filter_predicate(test_cond_type_e type, test_cond_e cond, uint8_t sst, uint32_t sd)
{
  test_info_lst_t dst = {0};

  dst.test_cond_type = type;
  // It can only be TRUE_TEST_COND_TYPE so it does not matter the type
  // but ugly ugly...
  dst.S_NSSAI = TRUE_TEST_COND_TYPE;

  dst.test_cond = calloc(1, sizeof(test_cond_e));
  assert(dst.test_cond != NULL && "Memory exhausted");
  *dst.test_cond = cond;

  dst.test_cond_value = calloc(1, sizeof(test_cond_value_t));
  assert(dst.test_cond_value != NULL && "Memory exhausted");
  dst.test_cond_value->type = OCTET_STRING_TEST_COND_VALUE;

  dst.test_cond_value->octet_string_value = calloc(1, sizeof(byte_array_t));
  assert(dst.test_cond_value->octet_string_value != NULL && "Memory exhausted");
  const size_t len_nssai = 4;
  dst.test_cond_value->octet_string_value->len = len_nssai;
  dst.test_cond_value->octet_string_value->buf = calloc(len_nssai, sizeof(uint8_t));
  assert(dst.test_cond_value->octet_string_value->buf != NULL && "Memory exhausted");

  dst.test_cond_value->octet_string_value->buf[0] = sst;
  dst.test_cond_value->octet_string_value->buf[1] = (sd >> 16) & 0xff;
  dst.test_cond_value->octet_string_value->buf[2] = (sd >> 8) & 0xff;
  dst.test_cond_value->octet_string_value->buf[3] = sd & 0xff;

  return dst;
}

static
label_info_lst_t fill_kpm_label(void)
{
  label_info_lst_t label_item = {0};

  label_item.noLabel = ecalloc(1, sizeof(enum_value_e));
  *label_item.noLabel = TRUE_ENUM_VALUE;

  return label_item;
}

static
kpm_act_def_format_1_t fill_act_def_frm_1(ric_report_style_item_t const* report_item)
{
  assert(report_item != NULL);

  kpm_act_def_format_1_t ad_frm_1 = {0};

  size_t const sz = report_item->meas_info_for_action_lst_len;

  /* The DQN requires only the instantaneous RLC DL queue occupancy.
   * Do not request unrelated delta-based KPM measurements here.
   */
  size_t queue_idx = sz;

  for (size_t i = 0; i < sz; ++i) {
    if (cmp_str_ba("OAI.RlcTxQueuePktsDl",
                   report_item->meas_info_for_action_lst[i].name) == 0) {
      queue_idx = i;
      break;
    }
  }

  assert(queue_idx < sz &&
         "OAI.RlcTxQueuePktsDl not advertised by the E2 node");

  ad_frm_1.meas_info_lst_len = 1;
  ad_frm_1.meas_info_lst =
      calloc(1, sizeof(meas_info_format_1_lst_t));
  assert(ad_frm_1.meas_info_lst != NULL && "Memory exhausted");

  meas_info_format_1_lst_t* meas_item =
      &ad_frm_1.meas_info_lst[0];

  meas_item->meas_type.type = NAME_MEAS_TYPE;
  meas_item->meas_type.name =
      copy_byte_array(report_item->meas_info_for_action_lst[queue_idx].name);

  meas_item->label_info_lst_len = 1;
  meas_item->label_info_lst =
      ecalloc(1, sizeof(label_info_lst_t));
  meas_item->label_info_lst[0] = fill_kpm_label();

  // 8.3.8 [0, 4294967295]
  ad_frm_1.gran_period_ms = period_ms;

  // 8.3.20 - OPTIONAL
  ad_frm_1.cell_global_id = NULL;

#if defined KPM_V2_03 || defined KPM_V3_00
  // [0, 65535]
  ad_frm_1.meas_bin_range_info_lst_len = 0;
  ad_frm_1.meas_bin_info_lst = NULL;
#endif

  return ad_frm_1;
}

static
kpm_act_def_t fill_report_style_4(ric_report_style_item_t const* report_item,
                                  uint8_t sst,
                                  uint32_t sd)
{
  assert(report_item != NULL);
  assert(report_item->act_def_format_type == FORMAT_4_ACTION_DEFINITION);

  kpm_act_def_t act_def = {.type = FORMAT_4_ACTION_DEFINITION};

  // Fill matching condition
  // [1, 32768]
  act_def.frm_4.matching_cond_lst_len = 1;
  act_def.frm_4.matching_cond_lst = calloc(act_def.frm_4.matching_cond_lst_len, sizeof(matching_condition_format_4_lst_t));
  assert(act_def.frm_4.matching_cond_lst != NULL && "Memory exhausted");
  // Filter connected UEs by S-NSSAI criteria
  test_cond_type_e const type = S_NSSAI_TEST_COND_TYPE; // CQI_TEST_COND_TYPE
  test_cond_e const condition = EQUAL_TEST_COND; // GREATERTHAN_TEST_COND
  act_def.frm_4.matching_cond_lst[0].test_info_lst =
      filter_predicate(type, condition, sst, sd);

  printf("[xApp KPM]: S-NSSAI filter SST=%u SD=%06x\n",
         sst, sd);

  // Fill Action Definition Format 1
  // 8.2.1.2.1
  act_def.frm_4.action_def_format_1 = fill_act_def_frm_1(report_item);

  return act_def;
}

typedef kpm_act_def_t (*fill_kpm_act_def)(ric_report_style_item_t const* report_item,
                                           uint8_t sst,
                                           uint32_t sd);

static
fill_kpm_act_def get_kpm_act_def[END_RIC_SERVICE_REPORT] = {
    NULL,
    NULL,
    NULL,
    fill_report_style_4,
    NULL,
};

static
kpm_sub_data_t gen_kpm_subs(kpm_ran_function_def_t const* ran_func,
                            uint8_t sst,
                            uint32_t sd)
{
  assert(ran_func != NULL);
  assert(ran_func->ric_event_trigger_style_list != NULL);

  kpm_sub_data_t kpm_sub = {0};

  // Generate Event Trigger
  assert(ran_func->ric_event_trigger_style_list[0].format_type == FORMAT_1_RIC_EVENT_TRIGGER);
  kpm_sub.ev_trg_def.type = FORMAT_1_RIC_EVENT_TRIGGER;
  kpm_sub.ev_trg_def.kpm_ric_event_trigger_format_1.report_period_ms = period_ms;

  // Generate Action Definition
  kpm_sub.sz_ad = 1;
  kpm_sub.ad = calloc(kpm_sub.sz_ad, sizeof(kpm_act_def_t));
  assert(kpm_sub.ad != NULL && "Memory exhausted");

  // Multiple Action Definitions in one SUBSCRIPTION message is not supported in this project
  // Multiple REPORT Styles = Multiple Action Definition = Multiple SUBSCRIPTION messages
  ric_report_style_item_t const* report_item = NULL;
  ric_service_report_e report_style_type = END_RIC_SERVICE_REPORT;

  for (size_t i = 0; i < ran_func->sz_ric_report_style_list; ++i) {
    ric_service_report_e const candidate =
        ran_func->ric_report_style_list[i].report_style_type;

    if ((size_t)candidate < END_RIC_SERVICE_REPORT &&
        get_kpm_act_def[candidate] != NULL) {
      report_item = &ran_func->ric_report_style_list[i];
      report_style_type = candidate;
      printf("[xApp KPM]: Selecting supported Report Style %d\n",
             (int)report_style_type + 1);
      break;
    }
  }

  assert(report_item != NULL &&
         "No supported KPM Report Style advertised by E2 node");

  *kpm_sub.ad = get_kpm_act_def[report_style_type](report_item, sst, sd);

  return kpm_sub;
}

static
bool eq_sm(sm_ran_function_t const* elem, int const id)
{
  if (elem->id == id)
    return true;

  return false;
}

static
size_t find_sm_idx(sm_ran_function_t* rf, size_t sz, bool (*f)(sm_ran_function_t const*, int const), int const id)
{
  for (size_t i = 0; i < sz; i++) {
    if (f(&rf[i], id))
      return i;
  }

  assert(0 != 0 && "SM ID could not be found in the RAN Function List");
}

typedef struct {
  uint8_t sst;
  uint32_t sd;
  const char* name;
} kpm_slice_filter_t;

static const kpm_slice_filter_t kpm_slice_filters[] = {
    {.sst = 1, .sd = 0x000001, .name = "eMBB"},
    {.sst = 2, .sd = 0x000002, .name = "URLLC"},
    {.sst = 3, .sd = 0x000003, .name = "mMTC"},
};

static const size_t num_kpm_slices =
    sizeof(kpm_slice_filters) / sizeof(kpm_slice_filters[0]);

int main(int argc, char* argv[])
{
  fr_args_t args = init_fr_args(argc, argv);

  // Init the xApp
  init_xapp_api(&args);
  sleep(1);

  e2_node_arr_xapp_t nodes = e2_nodes_xapp_api();
  defer({ free_e2_node_arr_xapp(&nodes); });

  assert(nodes.len > 0);

  /*
   * Current experiment controls one OAI gNB.
   *
   * Keep the design explicit rather than silently mixing KPM state from
   * multiple E2 nodes into one DQN state vector.
   */
  assert(nodes.len == 1 &&
         "Current DQN controller supports exactly one E2 node");

  dqn_ctrl_node_id = &nodes.n[0].id;

  printf("Connected E2 nodes = %d\n", nodes.len);

  dqn_ort_init();

  pthread_mutexattr_t attr = {0};
  int rc = pthread_mutex_init(&mtx, &attr);
  assert(rc == 0);

  sm_ans_xapp_t* hndl =
      calloc(nodes.len * num_kpm_slices, sizeof(sm_ans_xapp_t));
  assert(hndl != NULL);

  ////////////
  // START KPM
  ////////////
  int const KPM_ran_function = 2;

  for (size_t i = 0; i < nodes.len; ++i) {
    e2_node_connected_xapp_t* n = &nodes.n[i];

    size_t const idx = find_sm_idx(n->rf, n->len_rf, eq_sm, KPM_ran_function);
    assert(n->rf[idx].defn.type == KPM_RAN_FUNC_DEF_E && "KPM is not the received RAN Function");
    // if REPORT Service is supported by E2 node, send SUBSCRIPTION
    // e.g. OAI CU-CP
    if (n->rf[idx].defn.kpm.ric_report_style_list != NULL) {

      for (size_t s_idx = 0; s_idx < num_kpm_slices; ++s_idx) {

        const kpm_slice_filter_t* slice = &kpm_slice_filters[s_idx];

        printf("\n[xApp KPM]: Creating %s subscription "
               "SST=%u SD=%06x\n",
               slice->name,
               slice->sst,
               slice->sd);

        kpm_sub_data_t kpm_sub =
            gen_kpm_subs(&n->rf[idx].defn.kpm,
                         slice->sst,
                         slice->sd);

        size_t const h_idx = i * num_kpm_slices + s_idx;

        hndl[h_idx] =
            report_sm_xapp_api(&n->id,
                               KPM_ran_function,
                               &kpm_sub,
                               kpm_slice_callbacks[s_idx]);

        assert(hndl[h_idx].success == true);

        free_kpm_sub_data(&kpm_sub);
      }
    }
  }
  ////////////
  // END KPM
  ////////////

  sleep(10);

  for (size_t i = 0; i < nodes.len; ++i) {
    for (size_t s_idx = 0; s_idx < num_kpm_slices; ++s_idx) {
      size_t const h_idx = i * num_kpm_slices + s_idx;

      if (hndl[h_idx].success == true)
        rm_report_sm_xapp_api(hndl[h_idx].u.handle);
    }
  }
  free(hndl);

  // Stop the xApp
  while (try_stop_xapp_api() == false)
    usleep(1000);

  dqn_ort_shutdown();

  printf("Test xApp run SUCCESSFULLY\n");
}

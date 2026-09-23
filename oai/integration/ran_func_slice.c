/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "ran_func_slice.h"
#include "../../flexric/test/rnd/fill_rnd_data_slice.h"

#include "common/ran_context.h"
#include "openair2/LAYER2/NR_MAC_gNB/nr_mac_gNB.h"
#include "openair2/LAYER2/NR_MAC_gNB/nr_slice_manager.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

bool read_slice_sm(void* data)
{
  assert(data != NULL);
//  assert(data->type == SLICE_STATS_V0);

  slice_ind_data_t* slice = (slice_ind_data_t*)data;
  fill_slice_ind_data(slice);

  return true;
}

void read_slice_setup_sm(void* data)
{
  assert(data != NULL);
//  assert(data->type == SLICE_AGENT_IF_E2_SETUP_ANS_V0 );

  assert(0 !=0 && "Not supported");
}

sm_ag_if_ans_t write_ctrl_slice_sm(void const* data)
{
  assert(data != NULL);

  slice_ctrl_req_data_t const* slice_req_ctrl =
      (slice_ctrl_req_data_t const*)data;

  slice_ctrl_msg_t const* msg = &slice_req_ctrl->msg;

  if (msg->type == SLICE_CTRL_SM_V0_ADD) {

    printf("[E2 Agent]: SLICE CONTROL ADD rx\n");

    /*
     * DQN/FlexRIC transport convention used by this prototype:
     *
     *   Slice-SM ID 0 -> eMBB
     *   Slice-SM ID 1 -> URLLC
     *   Slice-SM ID 2 -> mMTC
     *
     * Each entry must use:
     *
     *   SLICE_ALG_SM_V0_NVS
     *   SLICE_SM_NVS_V0_CAPACITY
     *
     * pct_reserved carries the DQN resource-sharing weight as
     * a fraction in [0.0, 1.0].
     *
     * These values are NOT literal PRB counts.
     */

    const ul_dl_slice_conf_t *dl = &msg->u.add_mod_slice.dl;

    bool valid = true;
    bool seen[NR_SLICE_COUNT] = {false, false, false};
    uint8_t weights[NR_SLICE_COUNT] = {0, 0, 0};

    if (dl->len_slices != NR_SLICE_COUNT) {
      printf("[DQN SLICE CTRL] REJECT: expected %d DL slices, received %u\n",
             NR_SLICE_COUNT,
             dl->len_slices);
      valid = false;
    }

    if (valid) {
      for (uint32_t i = 0; i < dl->len_slices; ++i) {

        const fr_slice_t *slice = &dl->slices[i];

        if (slice->id >= NR_SLICE_COUNT) {
          printf("[DQN SLICE CTRL] REJECT: unknown slice id=%u\n",
                 slice->id);
          valid = false;
          break;
        }

        if (seen[slice->id]) {
          printf("[DQN SLICE CTRL] REJECT: duplicate slice id=%u\n",
                 slice->id);
          valid = false;
          break;
        }

        if (slice->params.type != SLICE_ALG_SM_V0_NVS) {
          printf("[DQN SLICE CTRL] REJECT: id=%u is not NVS\n",
                 slice->id);
          valid = false;
          break;
        }

        if (slice->params.u.nvs.conf != SLICE_SM_NVS_V0_CAPACITY) {
          printf("[DQN SLICE CTRL] REJECT: id=%u is not NVS CAPACITY\n",
                 slice->id);
          valid = false;
          break;
        }

        float const pct =
            slice->params.u.nvs.u.capacity.u.pct_reserved;

        if (pct < 0.0f || pct > 1.0f) {
          printf("[DQN SLICE CTRL] REJECT: id=%u pct_reserved=%f outside [0,1]\n",
                 slice->id,
                 pct);
          valid = false;
          break;
        }

        /*
         * Convert the wire representation [0.0,1.0] to the exact
         * integer DQN weight representation [0,100].
         *
         * +0.5f provides nearest-integer conversion without requiring
         * a libm dependency.
         */
        uint8_t const weight =
            (uint8_t)(pct * 100.0f + 0.5f);

        weights[slice->id] = weight;
        seen[slice->id] = true;

        printf("[DQN SLICE CTRL] id=%u pct_reserved=%.3f -> weight=%u\n",
               slice->id,
               pct,
               weight);
      }
    }

    if (valid) {
      for (int i = 0; i < NR_SLICE_COUNT; ++i) {
        if (!seen[i]) {
          printf("[DQN SLICE CTRL] REJECT: missing slice id=%d\n", i);
          valid = false;
        }
      }
    }

    if (valid) {

      gNB_MAC_INST *mac = RC.nrmac[0];

      if (mac == NULL) {

        printf("[DQN SLICE CTRL] REJECT: MAC instance unavailable\n");

      } else {

        bool const accepted =
            nr_slice_manager_update(&mac->slice_manager,
                                    weights[NR_SLICE_EMBB],
                                    weights[NR_SLICE_URLLC],
                                    weights[NR_SLICE_MMTC]);

        if (accepted) {

          nr_slice_policy_t policy = {0};
          nr_slice_manager_snapshot(&mac->slice_manager, &policy);

          printf("[DQN SLICE CTRL] POLICY INSTALLED "
                 "[eMBB=%u, URLLC=%u, mMTC=%u]\n",
                 policy.weight[NR_SLICE_EMBB],
                 policy.weight[NR_SLICE_URLLC],
                 policy.weight[NR_SLICE_MMTC]);

        } else {

          printf("[DQN SLICE CTRL] REJECT: vector [%u,%u,%u] "
                 "is not in the frozen DQN action space\n",
                 weights[NR_SLICE_EMBB],
                 weights[NR_SLICE_URLLC],
                 weights[NR_SLICE_MMTC]);
        }
      }
    }

  } else if (msg->type == SLICE_CTRL_SM_V0_DEL) {

    printf("[E2 Agent]: SLICE CONTROL DEL rx "
           "(DQN policy unchanged)\n");

  } else if (msg->type == SLICE_CTRL_SM_V0_UE_SLICE_ASSOC) {

    printf("[E2 Agent]: SLICE CONTROL ASSOC rx "
           "(DQN uses MAC NSSAI classification)\n");

  } else {

    printf("[E2 Agent]: unknown SLICE CONTROL type=%d\n",
           msg->type);
  }

  sm_ag_if_ans_t ans = {
      .type = CTRL_OUTCOME_SM_AG_IF_ANS_V0
  };

  ans.ctrl_out.type = SLICE_AGENT_IF_CTRL_ANS_V0;

  return ans;
}


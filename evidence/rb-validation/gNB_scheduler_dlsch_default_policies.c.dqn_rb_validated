/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief       Default pluggable policy functions for the DL scheduler pipeline.
 *
 * These are the built-in implementations behind the function pointers
 * (dl_ri_pmi_select, dl_tda_select, dl_beam_select, dl_mcs_select, dl_rb_alloc, dl_lcid_alloc)
 * wired up at MAC init time.  They can be replaced at runtime by external
 * scheduler plug-ins without touching the core scheduling loop in
 * gNB_scheduler_dlsch.c.
 */

#include "common/utils/nr/nr_common.h"
#include "common/utils/LOG/log.h"
#include "gNB_scheduler_dlsch_default_policies.h"
/*MAC*/
#include "NR_MAC_COMMON/nr_mac.h"
#include "NR_MAC_gNB/nr_mac_gNB.h"
#include "LAYER2/NR_MAC_gNB/mac_proto.h"
#include "openair2/LAYER2/nr_rlc/nr_rlc_oai_api.h"

/*TAG*/
#include "NR_TAG-Id.h"

/*Softmodem params*/
#include "executables/softmodem-common.h"
#include "../../../nfapi/oai_integration/vendor_ext.h"

// Default RI/PMI selector: reads rank and PMI from CSI feedback for new-tx,
// or from HARQ process state for retx.
void nr_dl_ri_pmi_select_default(const gNB_MAC_INST *mac, nr_dl_candidate_t *candidates, int n_candidates)
{
  FOR_EACH_CANDIDATE(cand, candidates, n_candidates)
  {
    NR_UE_sched_ctrl_t *sched_ctrl = &cand->UE->UE_sched_ctrl;
    NR_UE_DL_BWP_t *dl_bwp = &cand->UE->current_DL_BWP;
    if (cand->is_retx) {
      cand->sched_pdsch.nrOfLayers = sched_ctrl->harq_processes[cand->retx_harq_pid].sched_pdsch.nrOfLayers;
      cand->sched_pdsch.pm_index =
          get_pm_index(mac, cand->UE, dl_bwp->dci_format, cand->sched_pdsch.nrOfLayers, mac->radio_config.pdsch_AntennaPorts.XP);
    } else {
      cand->sched_pdsch.nrOfLayers = cand->csi_ri + 1;
      cand->sched_pdsch.pm_index = cand->csi_pm_index;
    }
  }
}

// Default TDA selector: picks the slot-wide TDA index from get_dl_tda(),
// then resolves tda_info per candidate using each UE's own BWP / search
// space / coreset. Marks invalids with skipped=true.
int nr_dl_tda_select_default(const gNB_MAC_INST *mac, nr_dl_candidate_t *candidates, int n_candidates, frame_t frame, slot_t slot)
{
  int tda = get_dl_tda(mac, slot);
  AssertFatal(tda >= 0, "Unable to find PDSCH time domain allocation in list\n");
  const NR_ServingCellConfigCommon_t *scc = mac->common_channels[0].ServingCellConfigCommon;

  int n_valid = 0;
  FOR_EACH_CANDIDATE(cand, candidates, n_candidates)
  {
    if (cand->skipped)
      continue;
    NR_UE_info_t *UE = cand->UE;
    NR_UE_sched_ctrl_t *sched_ctrl = &UE->UE_sched_ctrl;
    NR_UE_DL_BWP_t *dl_bwp = &UE->current_DL_BWP;
    int coresetid = sched_ctrl->coreset->controlResourceSetId;
    NR_tda_info_t tda_info = get_dl_tda_info(dl_bwp,
                                             sched_ctrl->search_space->searchSpaceType->present,
                                             tda,
                                             scc->dmrs_TypeA_Position,
                                             1,
                                             TYPE_C_RNTI_,
                                             coresetid,
                                             false);
    if (!tda_info.valid_tda) {
      cand->skipped = true;
      continue;
    }

    /* For retransmissions with a changed TDA, refit rbSize to preserve TBS */
    if (cand->is_retx) {
      const NR_sched_pdsch_t *orig = &sched_ctrl->harq_processes[cand->retx_harq_pid].sched_pdsch;
      bool tda_changed =
          tda_info.startSymbolIndex != orig->tda_info.startSymbolIndex || tda_info.nrOfSymbols != orig->tda_info.nrOfSymbols;
      if (tda_changed) {
        uint16_t new_rbSize = check_dl_retx_feasibility(cand, tda, &tda_info, scc, dl_bwp->BWPSize);
        if (!new_rbSize) {
          cand->skipped = true;
          continue;
        }
        cand->retx_rbSize = new_rbSize;
      }
    }

    cand->sched_pdsch.time_domain_allocation = tda;
    cand->sched_pdsch.tda_info = tda_info;
    cand->alloc_slbitmap = SL_to_bitmap(tda_info.startSymbolIndex, tda_info.nrOfSymbols);
    n_valid++;
  }
  return n_valid;
}

static int compare_dl_pf_ptrs(const void *a, const void *b)
{
  const nr_dl_candidate_t *ca = *(const nr_dl_candidate_t *const *)a;
  const nr_dl_candidate_t *cb = *(const nr_dl_candidate_t *const *)b;
  /* retx first (INFINITY weight), then highest PF weight */
  float wa = ca->is_retx ? INFINITY : dl_pf_weight(ca->current_mcs, ca->mcs_table, ca->sched_pdsch.nrOfLayers, ca->avg_throughput);
  float wb = cb->is_retx ? INFINITY : dl_pf_weight(cb->current_mcs, cb->mcs_table, cb->sched_pdsch.nrOfLayers, cb->avg_throughput);
  return (wa < wb) - (wa > wb);
}

int nr_dl_beam_select_default(NR_beam_info_t *beam_info,
                              const int16_t *beam_index_list,
                              nr_dl_candidate_t *candidates,
                              int n_candidates,
                              frame_t frame,
                              slot_t slot,
                              int slots_per_frame)
{
  /* Build pointer array sorted by PF priority so retx and high-priority UEs claim beams first. */
  nr_dl_candidate_t *order[MAX_MOBILES_PER_GNB];
  int n_active = 0;
  FOR_EACH_CANDIDATE(cand, candidates, n_candidates)
  if (!cand->skipped)
    order[n_active++] = cand;
  qsort(order, n_active, sizeof(*order), compare_dl_pf_ptrs);

  int n_valid = 0;
  for (int i = 0; i < n_active; i++) {
    nr_dl_candidate_t *cand = order[i];
    NR_beam_alloc_t beam = beam_allocation_procedure(beam_info, frame, slot, cand->alloc_beam_dir, slots_per_frame);
    if (beam.idx < 0) {
      cand->skipped = true;
      continue;
    }

    cand->alloc_beam_idx = beam.idx;
    cand->alloc_new_beam = beam.new_beam;
    n_valid++;
  }
  return n_valid;
}

void nr_dl_mcs_select_default(const gNB_MAC_INST *mac, nr_dl_candidate_t *candidates, int n_candidates)
{
  const NR_bler_options_t *bo = &mac->dl_bler;
  FOR_EACH_CANDIDATE(cand, candidates, n_candidates)
  {
    int mcs;
    if (cand->is_retx) {
      mcs = cand->current_mcs; /* retx MCS is fixed by the HARQ round */
    } else if (bo->harq_round_max == 1) {
      mcs = max(bo->min_mcs, min(bo->max_mcs, cand->max_mcs));
    } else if (!cand->bler_updated) {
      mcs = cand->current_mcs;
    } else {
      mcs = nr_adapt_mcs_from_bler(cand->current_mcs,
                                   bo->min_mcs,
                                   cand->max_mcs,
                                   cand->bler,
                                   bo->lower,
                                   bo->upper,
                                   cand->last_num_sched);
    }
    cand->sched_pdsch.mcs = mcs;
    /* Persist for all candidates — BLER-based MCS ramps even for UEs the
     * policy rejects this slot (failed CCE, no free RBs, etc.). */
    if (!cand->is_retx)
      cand->UE->UE_sched_ctrl.dl_bler_stats.mcs = mcs;
  }
}

static int compare_dl_pf_rb_ptrs(const void *a, const void *b)
{
  const nr_dl_candidate_t *ca = *(const nr_dl_candidate_t *const *)a;
  const nr_dl_candidate_t *cb = *(const nr_dl_candidate_t *const *)b;
  /* retx first, then highest PF weight (uses sched_pdsch.mcs, which is set by mcs_select) */
  float wa =
      ca->is_retx ? INFINITY : dl_pf_weight(ca->sched_pdsch.mcs, ca->mcs_table, ca->sched_pdsch.nrOfLayers, ca->avg_throughput);
  float wb =
      cb->is_retx ? INFINITY : dl_pf_weight(cb->sched_pdsch.mcs, cb->mcs_table, cb->sched_pdsch.nrOfLayers, cb->avg_throughput);
  return (wa < wb) - (wa > wb);
}

int nr_dl_proportional_fair(const nr_dl_sched_params_t *params, nr_dl_candidate_t *candidates, int n_candidates)
{
  const int min_rbSize = 5;
  int n_scheduled = 0;

  /*
   * DQN Phase-3 RB validation counters.
   *
   * Validation only: these counters do not participate in any scheduling
   * decision.  One log line is emitted for every 1000 DQN-controlled
   * Phase-3 PF invocations.
   */
  static unsigned long long dqn_rb_calls_window = 0;

  static unsigned long long dqn_rb_demand_window[NR_SLICE_COUNT] = {0};
  static unsigned long long dqn_rb_target_window[NR_SLICE_COUNT] = {0};
  static unsigned long long dqn_rb_protected_window[NR_SLICE_COUNT] = {0};
  static unsigned long long dqn_rb_effective_window[NR_SLICE_COUNT] = {0};

  static unsigned long long dqn_rb_pass1_window[NR_SLICE_COUNT] = {0};

  static unsigned long long dqn_rb_protected_used_window[NR_SLICE_COUNT] = {0};
  static unsigned long long dqn_rb_redistributed_used_window[NR_SLICE_COUNT] = {0};

  static unsigned long long dqn_rb_borrow_window[NR_SLICE_COUNT] = {0};

  static unsigned long long dqn_rb_unknown_borrow_window = 0;
  static unsigned long long dqn_rb_phase3_committed_window = 0;

  static unsigned int dqn_rb_policy_changes_window = 0;
  static uint8_t dqn_rb_last_weight[NR_SLICE_COUNT] = {0};

  /*
   * Capture one coherent DQN policy for the complete PF invocation.
   *
   * weight[] = [eMBB, URLLC, mMTC]
   *
   * These are relative resource-sharing weights, NOT literal PRB counts.
   */
  nr_slice_policy_t slice_policy = {0};
  nr_slice_manager_snapshot(&params->mac->slice_manager, &slice_policy);

  /* Build pointer array sorted by PF priority (retx first, then highest weight) */
  nr_dl_candidate_t *order[MAX_MOBILES_PER_GNB];
  int n_active = 0;

  FOR_EACH_CANDIDATE(cand, candidates, n_candidates)
  if (!cand->skipped)
    order[n_active++] = cand;

  qsort(order, n_active, sizeof(*order), compare_dl_pf_rb_ptrs);

  /*
   * Phase 1: HARQ retransmissions.
   *
   * Retransmissions deliberately remain outside DQN slice budgeting.
   */
  for (int j = 0; j < n_active; j++) {
    nr_dl_candidate_t *cand = order[j];

    if (!cand->is_retx)
      continue;

    int needed_rbs = cand->retx_rbSize;
    uint16_t *vrb_map = params->vrb_map[cand->alloc_beam_idx];
    int rbStart, rbSize;

    if (!get_rb_alloc(needed_rbs,
                      cand->bwp_size,
                      cand->bwp_start,
                      cand->bwp_size,
                      vrb_map,
                      cand->alloc_slbitmap,
                      &rbStart,
                      &rbSize))
      continue;

    COMMIT_ALLOC(params,
                 cand,
                 rbStart,
                 needed_rbs,
                 cand->sched_pdsch.mcs,
                 n_scheduled);
  }

  /*
   * Phase 2: no-RLC-data candidates.
   *
   * TA commands / beam-switch MAC CEs also deliberately remain outside
   * DQN slice budgeting.
   */
  for (int j = 0; j < n_active; j++) {
    nr_dl_candidate_t *cand = order[j];

    if (cand->is_retx || cand->pending_bytes > 0)
      continue;

    uint16_t *vrb_map = params->vrb_map[cand->alloc_beam_idx];
    int rbStart, rbSize;

    if (!get_rb_alloc(min_rbSize,
                      cand->bwp_size,
                      cand->bwp_start,
                      cand->bwp_size,
                      vrb_map,
                      cand->alloc_slbitmap,
                      &rbStart,
                      &rbSize))
      continue;

    COMMIT_ALLOC(params,
                 cand,
                 rbStart,
                 min_rbSize,
                 cand->sched_pdsch.mcs,
                 n_scheduled);
  }

  /*
   * The existing OAI policy uses beam-0 bandwidth as the scheduling ceiling.
   *
   * Actual feasibility is still determined later by get_rb_alloc(), which
   * checks the VRB map and candidate TDA symbol mask.
   */
  int max_rbSize = params->n_rb_avail[0];
  DevAssert(max_rbSize >= min_rbSize);

  int n_remain_ue = params->max_num_ue - n_scheduled;

  if (n_remain_ue <= 0)
    return n_scheduled;

  /*
   * Determine Phase-3 RB demand of each PF-ranked candidate.
   */
  uint16_t rbs_ue[MAX_MOBILES_PER_GNB] = {0};

  for (int j = 0; j < n_active; j++) {
    nr_dl_candidate_t *cand = order[j];

    if (cand->is_retx || cand->pending_bytes == 0)
      continue;

    int mcs = cand->sched_pdsch.mcs;
    uint8_t Qm = nr_get_Qm_dl(mcs, cand->mcs_table);
    uint16_t R = nr_get_code_rate_dl(mcs, cand->mcs_table);

    NR_pdsch_dmrs_t dmrs =
        get_dl_dmrs_params(params->mac->common_channels->ServingCellConfigCommon,
                           &cand->UE->current_DL_BWP,
                           &cand->sched_pdsch.tda_info,
                           cand->sched_pdsch.nrOfLayers);

    const int oh =
        3 * 4 + (cand->UE->UE_sched_ctrl.ta_apply ? 2 : 0);

    uint32_t tbs;

    nr_find_nb_rb(Qm,
                  R,
                  1,
                  cand->sched_pdsch.nrOfLayers,
                  cand->sched_pdsch.tda_info.nrOfSymbols,
                  dmrs.N_PRB_DMRS * dmrs.N_DMRS_SLOT,
                  cand->pending_bytes + oh,
                  min_rbSize,
                  max_rbSize,
                  &tbs,
                  &rbs_ue[j]);
  }

  const int total_weight =
      slice_policy.weight[NR_SLICE_EMBB]
      + slice_policy.weight[NR_SLICE_URLLC]
      + slice_policy.weight[NR_SLICE_MMTC];

  /*
   * If no DQN policy is installed, preserve the original OAI PF behavior.
   *
   * The all-zero DQN action also has no meaningful relative share. For this
   * deployment we interpret it as "no slice preference" and fall back to the
   * normal work-conserving PF allocator instead of intentionally idling RBs.
   */
  if (!slice_policy.enabled || total_weight == 0) {

    int n_rb_per_ue =
        max(min_rbSize, max_rbSize / n_remain_ue);

    int excess_total_rbs = max_rbSize;

    for (int j = 0, n = 0;
         j < n_active && n < n_remain_ue + 2;
         j++) {

      nr_dl_candidate_t *cand = order[j];

      if (cand->is_retx ||
          cand->pending_bytes == 0 ||
          rbs_ue[j] == 0)
        continue;

      if (n < n_remain_ue) {
        excess_total_rbs -=
            min(rbs_ue[j], n_rb_per_ue);

        excess_total_rbs =
            max(excess_total_rbs, 0);
      }

      n++;
    }

    for (int j = 0; j < n_active; j++) {

      nr_dl_candidate_t *cand = order[j];

      if (cand->is_retx ||
          cand->pending_bytes == 0 ||
          rbs_ue[j] == 0)
        continue;

      int rb_req =
          min(rbs_ue[j], n_rb_per_ue);

      int excess_req =
          max((int)rbs_ue[j] - rb_req, 0);

      if (excess_total_rbs > 0 &&
          excess_req > 0) {

        int excess_ack =
            min(excess_total_rbs, excess_req);

        rb_req += excess_ack;
        excess_total_rbs -= excess_ack;
      }

      int rbStart, rbSize;

      uint16_t *vrb_map =
          params->vrb_map[cand->alloc_beam_idx];

      if (!get_rb_alloc(min_rbSize,
                        rb_req,
                        cand->bwp_start,
                        cand->bwp_size,
                        vrb_map,
                        cand->alloc_slbitmap,
                        &rbStart,
                        &rbSize))
        continue;

      COMMIT_ALLOC(params,
                   cand,
                   rbStart,
                   rbSize,
                   cand->sched_pdsch.mcs,
                   n_scheduled);
    }

    return n_scheduled;
  }

  /*
   * --------------------------------------------------------------
   * DQN WORK-CONSERVING PHASE-3 POLICY
   * --------------------------------------------------------------
   *
   * 1. Derive protected shares from DQN weights.
   * 2. Cap each share by actual slice demand.
   * 3. Put unused capacity in a common pool.
   * 4. Redistribute that pool to slices that remain backlogged.
   *
   * Therefore a high URLLC weight protects URLLC when it is busy,
   * but idle URLLC capacity never has to remain permanently reserved.
   */

  /*
   * Flush the previous complete measurement window before accounting the
   * current invocation. This also means early returns later in the function
   * cannot lose already committed RB accounting.
   */
  if (dqn_rb_calls_window >= 1000) {

    LOG_I(NR_MAC,
          "[DQN RB VALID] calls=%llu policy_changes=%u "
          "last_weight=[%u,%u,%u] "
          "demand=[%llu,%llu,%llu] "
          "target=[%llu,%llu,%llu] "
          "protected=[%llu,%llu,%llu] "
          "effective=[%llu,%llu,%llu] "
          "pass1=[%llu,%llu,%llu] "
          "protected_used=[%llu,%llu,%llu] "
          "redistributed_used=[%llu,%llu,%llu] "
          "borrow=[%llu,%llu,%llu] "
          "unknown_borrow=%llu phase3_committed=%llu\n",
          dqn_rb_calls_window,
          dqn_rb_policy_changes_window,
          dqn_rb_last_weight[NR_SLICE_EMBB],
          dqn_rb_last_weight[NR_SLICE_URLLC],
          dqn_rb_last_weight[NR_SLICE_MMTC],
          dqn_rb_demand_window[NR_SLICE_EMBB],
          dqn_rb_demand_window[NR_SLICE_URLLC],
          dqn_rb_demand_window[NR_SLICE_MMTC],
          dqn_rb_target_window[NR_SLICE_EMBB],
          dqn_rb_target_window[NR_SLICE_URLLC],
          dqn_rb_target_window[NR_SLICE_MMTC],
          dqn_rb_protected_window[NR_SLICE_EMBB],
          dqn_rb_protected_window[NR_SLICE_URLLC],
          dqn_rb_protected_window[NR_SLICE_MMTC],
          dqn_rb_effective_window[NR_SLICE_EMBB],
          dqn_rb_effective_window[NR_SLICE_URLLC],
          dqn_rb_effective_window[NR_SLICE_MMTC],
          dqn_rb_pass1_window[NR_SLICE_EMBB],
          dqn_rb_pass1_window[NR_SLICE_URLLC],
          dqn_rb_pass1_window[NR_SLICE_MMTC],
          dqn_rb_protected_used_window[NR_SLICE_EMBB],
          dqn_rb_protected_used_window[NR_SLICE_URLLC],
          dqn_rb_protected_used_window[NR_SLICE_MMTC],
          dqn_rb_redistributed_used_window[NR_SLICE_EMBB],
          dqn_rb_redistributed_used_window[NR_SLICE_URLLC],
          dqn_rb_redistributed_used_window[NR_SLICE_MMTC],
          dqn_rb_borrow_window[NR_SLICE_EMBB],
          dqn_rb_borrow_window[NR_SLICE_URLLC],
          dqn_rb_borrow_window[NR_SLICE_MMTC],
          dqn_rb_unknown_borrow_window,
          dqn_rb_phase3_committed_window);

    dqn_rb_calls_window = 0;
    dqn_rb_policy_changes_window = 0;
    dqn_rb_unknown_borrow_window = 0;
    dqn_rb_phase3_committed_window = 0;

    for (int sid = 0; sid < NR_SLICE_COUNT; sid++) {
      dqn_rb_demand_window[sid] = 0;
      dqn_rb_target_window[sid] = 0;
      dqn_rb_protected_window[sid] = 0;
      dqn_rb_effective_window[sid] = 0;
      dqn_rb_pass1_window[sid] = 0;
      dqn_rb_protected_used_window[sid] = 0;
      dqn_rb_redistributed_used_window[sid] = 0;
      dqn_rb_borrow_window[sid] = 0;
    }
  }

  if (dqn_rb_calls_window == 0) {

    for (int sid = 0; sid < NR_SLICE_COUNT; sid++)
      dqn_rb_last_weight[sid] = slice_policy.weight[sid];

  } else if (dqn_rb_last_weight[NR_SLICE_EMBB] != slice_policy.weight[NR_SLICE_EMBB] ||
             dqn_rb_last_weight[NR_SLICE_URLLC] != slice_policy.weight[NR_SLICE_URLLC] ||
             dqn_rb_last_weight[NR_SLICE_MMTC] != slice_policy.weight[NR_SLICE_MMTC]) {

    dqn_rb_policy_changes_window++;

    for (int sid = 0; sid < NR_SLICE_COUNT; sid++)
      dqn_rb_last_weight[sid] = slice_policy.weight[sid];
  }

  dqn_rb_calls_window++;

  int slice_demand[NR_SLICE_COUNT] = {0};
  int slice_candidates[NR_SLICE_COUNT] = {0};

  for (int j = 0; j < n_active; j++) {

    nr_dl_candidate_t *cand = order[j];

    if (cand->is_retx ||
        cand->pending_bytes == 0 ||
        rbs_ue[j] == 0)
      continue;

    nr_slice_id_t sid =
        nr_slice_manager_classify(cand->nssai);

    if (sid == NR_SLICE_UNKNOWN)
      continue;

    slice_demand[sid] += rbs_ue[j];
    slice_candidates[sid]++;
  }

  for (int sid = 0; sid < NR_SLICE_COUNT; sid++)
    dqn_rb_demand_window[sid] += slice_demand[sid];

  /*
   * Initial weighted shares using largest-remainder integer rounding.
   */
  int slice_budget[NR_SLICE_COUNT] = {0};
  int remainder[NR_SLICE_COUNT] = {0};
  int assigned = 0;

  for (int sid = 0; sid < NR_SLICE_COUNT; sid++) {

    int product =
        max_rbSize * slice_policy.weight[sid];

    slice_budget[sid] =
        product / total_weight;

    remainder[sid] =
        product % total_weight;

    assigned += slice_budget[sid];
  }

  int residual =
      max_rbSize - assigned;

  while (residual > 0) {

    int best = -1;

    for (int sid = 0; sid < NR_SLICE_COUNT; sid++) {

      if (slice_policy.weight[sid] == 0)
        continue;

      if (best < 0 ||
          remainder[sid] > remainder[best])
        best = sid;
    }

    if (best < 0)
      break;

    slice_budget[best]++;
    remainder[best] = -1;
    residual--;
  }

  /*
   * Record the original DQN-derived integer RB target before demand capping.
   */
  for (int sid = 0; sid < NR_SLICE_COUNT; sid++)
    dqn_rb_target_window[sid] += slice_budget[sid];

  /*
   * A slice cannot consume more than its current Phase-3 demand.
   * Anything it does not need immediately returns to the common pool.
   */
  int common_pool = 0;

  for (int sid = 0; sid < NR_SLICE_COUNT; sid++) {

    if (slice_budget[sid] > slice_demand[sid]) {

      common_pool +=
          slice_budget[sid] - slice_demand[sid];

      slice_budget[sid] =
          slice_demand[sid];
    }
  }

  /*
   * This is the demand-capped protected budget, before any common-pool
   * redistribution.
   *
   * Keep a per-invocation copy so successful first-pass allocations can be
   * split precisely into protected use versus redistributed common-pool use.
   */
  int slice_protected_budget[NR_SLICE_COUNT];

  for (int sid = 0; sid < NR_SLICE_COUNT; sid++) {
    slice_protected_budget[sid] = slice_budget[sid];
    dqn_rb_protected_window[sid] += slice_budget[sid];
  }

  /*
   * First redistribute the common pool among positive-weight slices that
   * remain backlogged. Weighted round-robin approximates the DQN proportions
   * while remaining deterministic and integer-only.
   */
  while (common_pool > 0) {

    bool progress = false;

    for (int sid = 0;
         sid < NR_SLICE_COUNT && common_pool > 0;
         sid++) {

      int tickets =
          slice_policy.weight[sid];

      if (tickets == 0)
        continue;

      for (int k = 0;
           k < tickets &&
           common_pool > 0 &&
           slice_budget[sid] < slice_demand[sid];
           k++) {

        slice_budget[sid]++;
        common_pool--;
        progress = true;
      }
    }

    if (!progress)
      break;
  }

  /*
   * If all positive-weight demand has already been satisfied, zero-weight
   * slices may borrow truly unused capacity instead of leaving radio
   * resources idle.
   */
  while (common_pool > 0) {

    bool progress = false;

    for (int sid = 0;
         sid < NR_SLICE_COUNT && common_pool > 0;
         sid++) {

      if (slice_budget[sid] >= slice_demand[sid])
        continue;

      slice_budget[sid]++;
      common_pool--;
      progress = true;
    }

    if (!progress)
      break;
  }

  /*
   * Final effective Phase-3 slice budget after all common-pool
   * redistribution.
   */
  for (int sid = 0; sid < NR_SLICE_COUNT; sid++)
    dqn_rb_effective_window[sid] += slice_budget[sid];

  /*
   * Allocate the final work-conserving per-slice budgets.
   *
   * Candidates remain in PF order. Within each slice, the remaining slice
   * budget is shared over the candidates still waiting in that slice.
   */
  int slice_used[NR_SLICE_COUNT] = {0};
  int slice_left[NR_SLICE_COUNT];

  for (int sid = 0; sid < NR_SLICE_COUNT; sid++)
    slice_left[sid] = slice_candidates[sid];

  for (int j = 0; j < n_active; j++) {

    nr_dl_candidate_t *cand = order[j];

    if (cand->is_retx ||
        cand->pending_bytes == 0 ||
        rbs_ue[j] == 0)
      continue;

    nr_slice_id_t sid =
        nr_slice_manager_classify(cand->nssai);

    /*
     * Unknown S-NSSAI is handled only after all mapped slices have received
     * their protected/borrowed opportunity.
     */
    if (sid == NR_SLICE_UNKNOWN)
      continue;

    int remaining =
        slice_budget[sid] - slice_used[sid];

    int peers =
        max(slice_left[sid], 1);

    slice_left[sid]--;

    if (remaining < min_rbSize)
      continue;

    int fair_share =
        max(min_rbSize, remaining / peers);

    int rb_req =
        min((int)rbs_ue[j],
            min(remaining, fair_share));

    if (rb_req < min_rbSize)
      continue;

    int rbStart, rbSize;

    uint16_t *vrb_map =
        params->vrb_map[cand->alloc_beam_idx];

    if (!get_rb_alloc(min_rbSize,
                      rb_req,
                      cand->bwp_start,
                      cand->bwp_size,
                      vrb_map,
                      cand->alloc_slbitmap,
                      &rbStart,
                      &rbSize))
      continue;

    /*
     * Do not use COMMIT_ALLOC here because that macro may return from this
     * function before slice_used[] is updated for the final scheduled UE.
     */
    cand->sched_pdsch.alloc_type = PDSCH_TYPE1;
    cand->sched_pdsch.rbStart = rbStart;
    cand->sched_pdsch.rbSize = rbSize;

    if (!commit_alloc(params, cand))
      continue;

    cand->scheduled = true;

    const int used_before = slice_used[sid];
    const int protected_remaining =
        max(slice_protected_budget[sid] - used_before, 0);

    const int protected_piece =
        min(rbSize, protected_remaining);

    const int redistributed_piece =
        rbSize - protected_piece;

    slice_used[sid] += rbSize;

    dqn_rb_pass1_window[sid] += rbSize;
    dqn_rb_protected_used_window[sid] += protected_piece;
    dqn_rb_redistributed_used_window[sid] += redistributed_piece;
    dqn_rb_phase3_committed_window += rbSize;

    n_scheduled++;

    if (n_scheduled >= params->max_num_ue)
      return n_scheduled;
  }

  /*
   * Final work-conserving borrowing pass.
   *
   * Every mapped slice has already received its DQN-controlled opportunity.
   * Any candidate that is still unscheduled may now borrow whatever physical
   * RB capacity remains available.
   *
   * Candidates are still visited in the same global PF order, so unused
   * capacity is redistributed according to the existing OAI PF priority.
   *
   * This includes:
   *   - mapped slices whose protected share was insufficient,
   *   - mapped slices that could not fully use their first-pass share because
   *     of VRB/TDA/CCE/PUCCH constraints,
   *   - unknown/unclassified S-NSSAI candidates.
   */
  for (int j = 0; j < n_active; j++) {

    nr_dl_candidate_t *cand = order[j];

    if (cand->scheduled ||
        cand->is_retx ||
        cand->pending_bytes == 0 ||
        rbs_ue[j] == 0)
      continue;

    int rbStart, rbSize;

    uint16_t *vrb_map =
        params->vrb_map[cand->alloc_beam_idx];

    if (!get_rb_alloc(min_rbSize,
                      rbs_ue[j],
                      cand->bwp_start,
                      cand->bwp_size,
                      vrb_map,
                      cand->alloc_slbitmap,
                      &rbStart,
                      &rbSize))
      continue;

    cand->sched_pdsch.alloc_type = PDSCH_TYPE1;
    cand->sched_pdsch.rbStart = rbStart;
    cand->sched_pdsch.rbSize = rbSize;

    if (!commit_alloc(params, cand))
      continue;

    cand->scheduled = true;

    nr_slice_id_t borrow_sid =
        nr_slice_manager_classify(cand->nssai);

    if (borrow_sid >= 0 && borrow_sid < NR_SLICE_COUNT)
      dqn_rb_borrow_window[borrow_sid] += rbSize;
    else
      dqn_rb_unknown_borrow_window += rbSize;

    dqn_rb_phase3_committed_window += rbSize;

    n_scheduled++;

    if (n_scheduled >= params->max_num_ue)
      return n_scheduled;
  }

  return n_scheduled;
}

void nr_dl_lcid_alloc_default(const gNB_MAC_INST *mac,
                              const nr_dl_candidate_t *candidate,
                              int tbs_available,
                              int lcid_alloc[NR_MAX_NUM_LCID])
{
  (void)mac;
  (void)tbs_available;
  memset(lcid_alloc, 0, NR_MAX_NUM_LCID * sizeof(int));
  for (int lcid = 0; lcid < NR_MAX_NUM_LCID; lcid++)
    lcid_alloc[lcid] = candidate->pending_bytes_per_lcid[lcid];
}

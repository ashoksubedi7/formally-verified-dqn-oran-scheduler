#ifndef NR_SLICE_MANAGER_H
#define NR_SLICE_MANAGER_H

#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>

#include "common/5g_platform_types.h"

/*
 * DQN slice ordering.
 *
 * This ordering MUST remain identical to the formally verified
 * DQN action table:
 *
 *   [eMBB, URLLC, mMTC]
 */
typedef enum {
  NR_SLICE_EMBB = 0,
  NR_SLICE_URLLC = 1,
  NR_SLICE_MMTC = 2,

  NR_SLICE_COUNT = 3,

  NR_SLICE_UNKNOWN = -1
} nr_slice_id_t;


/*
 * One coherent scheduler-policy snapshot.
 *
 * weight[] contains DQN resource-sharing weights in the range 0..100.
 * They are NOT literal PRB counts.
 */
typedef struct {
  bool enabled;
  uint8_t weight[NR_SLICE_COUNT];
} nr_slice_policy_t;


/*
 * Runtime slice manager.
 *
 * The complete three-weight policy is packed into one atomic value so
 * an E2 control update cannot expose a partially updated vector to the
 * MAC scheduling thread.
 */
typedef struct {
  atomic_uint_fast32_t packed_policy;
} nr_slice_manager_t;


/*
 * Initialize with slicing enforcement disabled.
 *
 * The normal OAI scheduler therefore remains unchanged until the first
 * valid policy has been installed.
 */
void nr_slice_manager_init(nr_slice_manager_t *manager);


/*
 * Atomically install a DQN policy.
 *
 * Valid DQN policies are:
 *   - weights in range [0,100]
 *   - sum == 100
 *
 * The all-zero vector is also permitted because action 0 exists in the
 * frozen DQN action space.
 *
 * Returns true when accepted.
 */
bool nr_slice_manager_update(nr_slice_manager_t *manager,
                             uint8_t embb_weight,
                             uint8_t urllc_weight,
                             uint8_t mmtc_weight);


/*
 * Read one coherent policy snapshot.
 */
void nr_slice_manager_snapshot(const nr_slice_manager_t *manager,
                               nr_slice_policy_t *policy);


/*
 * Convert an exact S-NSSAI into the verified DQN slice ordering.
 */
nr_slice_id_t nr_slice_manager_classify(nssai_t nssai);


/*
 * Obtain a slice weight from an already captured policy snapshot.
 *
 * Returns false when:
 *   - policy enforcement is disabled, or
 *   - the S-NSSAI is not one of the configured experiment slices.
 */
bool nr_slice_policy_get_weight(const nr_slice_policy_t *policy,
                                nssai_t nssai,
                                uint8_t *weight);


/*
 * Human-readable name used only for diagnostics/logging.
 */
const char *nr_slice_manager_slice_name(nr_slice_id_t slice);

#endif /* NR_SLICE_MANAGER_H */

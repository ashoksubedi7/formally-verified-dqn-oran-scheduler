/*
 * OAI NR MAC slice policy manager.
 *
 * This module is intentionally RF-backend independent:
 * it contains no RFsim, UHD, USRP, PHY, or radio-device assumptions.
 */

#include "NR_MAC_gNB/nr_slice_manager.h"

#include <assert.h>
#include <stddef.h>


/*
 * packed_policy layout:
 *
 * bits  0..7   eMBB weight
 * bits  8..15  URLLC weight
 * bits 16..23  mMTC weight
 * bit      24  policy enabled
 */
#define NR_SLICE_WEIGHT_MASK   0xffu
#define NR_SLICE_ENABLED_BIT   (1u << 24)


/*
 * Exact S-NSSAI mapping used by the current experiment:
 *
 * eMBB  : SST=1, SD=000001
 * URLLC : SST=2, SD=000002
 * mMTC  : SST=3, SD=000003
 */
#define NR_SLICE_EMBB_SST   1u
#define NR_SLICE_EMBB_SD    0x000001u

#define NR_SLICE_URLLC_SST  2u
#define NR_SLICE_URLLC_SD   0x000002u

#define NR_SLICE_MMTC_SST   3u
#define NR_SLICE_MMTC_SD    0x000003u


static uint32_t pack_policy(uint8_t embb,
                            uint8_t urllc,
                            uint8_t mmtc,
                            bool enabled)
{
  uint32_t packed = 0;

  packed |= (uint32_t)embb;
  packed |= (uint32_t)urllc << 8;
  packed |= (uint32_t)mmtc << 16;

  if (enabled)
    packed |= NR_SLICE_ENABLED_BIT;

  return packed;
}


void nr_slice_manager_init(nr_slice_manager_t *manager)
{
  assert(manager != NULL);

  /*
   * Disabled by default.
   *
   * This is important: merely compiling the slice manager into OAI must
   * not change existing scheduling behavior.
   */
  atomic_init(&manager->packed_policy, 0u);
}


bool nr_slice_manager_update(nr_slice_manager_t *manager,
                             uint8_t embb_weight,
                             uint8_t urllc_weight,
                             uint8_t mmtc_weight)
{
  assert(manager != NULL);

  if (embb_weight > 100 ||
      urllc_weight > 100 ||
      mmtc_weight > 100)
    return false;

  unsigned int const sum =
      (unsigned int)embb_weight +
      (unsigned int)urllc_weight +
      (unsigned int)mmtc_weight;

  /*
   * Exact frozen DQN action-space semantics:
   *
   *   action 0: [0,0,0]
   *
   * or:
   *
   *   every weight is a multiple of 20
   *   and the three weights sum to 100.
   *
   * This admits exactly the 22 actions used by the verified model and
   * rejects control vectors that were never part of that action space.
   */
  if (sum == 0)
    goto valid_policy;

  if (sum != 100)
    return false;

  if ((embb_weight % 20) != 0 ||
      (urllc_weight % 20) != 0 ||
      (mmtc_weight % 20) != 0)
    return false;

valid_policy:

  uint32_t const packed =
      pack_policy(embb_weight,
                  urllc_weight,
                  mmtc_weight,
                  true);

  /*
   * Release makes the complete new vector visible atomically.
   */
  atomic_store_explicit(&manager->packed_policy,
                        packed,
                        memory_order_release);

  return true;
}


void nr_slice_manager_snapshot(const nr_slice_manager_t *manager,
                               nr_slice_policy_t *policy)
{
  assert(manager != NULL);
  assert(policy != NULL);

  /*
   * One atomic load gives the MAC scheduler one coherent vector for the
   * scheduling decision.
   */
  uint32_t const packed =
      atomic_load_explicit(&manager->packed_policy,
                           memory_order_acquire);

  policy->enabled =
      (packed & NR_SLICE_ENABLED_BIT) != 0;

  policy->weight[NR_SLICE_EMBB] =
      (uint8_t)(packed & NR_SLICE_WEIGHT_MASK);

  policy->weight[NR_SLICE_URLLC] =
      (uint8_t)((packed >> 8) & NR_SLICE_WEIGHT_MASK);

  policy->weight[NR_SLICE_MMTC] =
      (uint8_t)((packed >> 16) & NR_SLICE_WEIGHT_MASK);
}


nr_slice_id_t nr_slice_manager_classify(nssai_t nssai)
{
  if (nssai.sst == NR_SLICE_EMBB_SST &&
      nssai.sd == NR_SLICE_EMBB_SD)
    return NR_SLICE_EMBB;

  if (nssai.sst == NR_SLICE_URLLC_SST &&
      nssai.sd == NR_SLICE_URLLC_SD)
    return NR_SLICE_URLLC;

  if (nssai.sst == NR_SLICE_MMTC_SST &&
      nssai.sd == NR_SLICE_MMTC_SD)
    return NR_SLICE_MMTC;

  return NR_SLICE_UNKNOWN;
}


bool nr_slice_policy_get_weight(const nr_slice_policy_t *policy,
                                nssai_t nssai,
                                uint8_t *weight)
{
  assert(policy != NULL);
  assert(weight != NULL);

  if (!policy->enabled)
    return false;

  nr_slice_id_t const slice =
      nr_slice_manager_classify(nssai);

  if (slice == NR_SLICE_UNKNOWN)
    return false;

  *weight = policy->weight[slice];

  return true;
}


const char *nr_slice_manager_slice_name(nr_slice_id_t slice)
{
  switch (slice) {

    case NR_SLICE_EMBB:
      return "eMBB";

    case NR_SLICE_URLLC:
      return "URLLC";

    case NR_SLICE_MMTC:
      return "mMTC";

    default:
      return "UNKNOWN";
  }
}

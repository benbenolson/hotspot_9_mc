#if 0
#ifndef SHARE_VM_RUNTIME_LATENCYSAMPLER_HPP
#define SHARE_VM_RUNTIME_LATENCYSAMPLER_HPP

#include "runtime/vm_operations.hpp"

/**********************************
 *      LATENCYSAMPLER            *
 *********************************/

struct regvals {
  int numcpu;
  uint64_t *vals;
};

#define mo_hex  0x01

class LatencySamplerTask : public PeriodicTask 
{     
  private:
    unsigned int highbit, lowbit;
    int mode;

    /* Used for reading from the registers */
    struct regvals *rdmsr(uint32_t reg, int cpu);
    struct regvals *rdmsr_on_all_cpus(uint32_t reg);
    uint64_t rdmsr_on_cpu(uint32_t reg, int cpu);


  public:
    bool is_active();
    LatencySamplerTask(int interval_time) : PeriodicTask(interval_time) {}
    void task();
};

class LatencySampler : AllStatic {
  friend class LatencySamplerTask;
  friend class SamplerThread;

  private:
    /* The task itself */
    static LatencySamplerTask* _task;

    /* Used for writing to the registers */
    void wrmsr(uint32_t reg, uint64_t regval, int cpu);
    void wrmsr_on_cpu(uint32_t reg, uint64_t regvals, int cpu);
    void wrmsr_on_all_cpus(uint32_t reg, uint64_t regvals);
    
    /* The register MSRs */
    unsigned long C0_CTL; /* The whole box's control register */
    unsigned long C0_CTL0; /* The control registers */
    unsigned long C0_CTL1;
    unsigned long C0_CTR0; /* The counter registers */
    unsigned long C0_CTR1;
    unsigned long C0_FILTER1; /* The filter register */

    /* The register values */
    unsigned long long BOX_RESET_AND_FREEZE;
    unsigned long long BOX_UNFREEZE;
    unsigned long long TOR_OCCUPANCY_MISS_OPCODE;
    unsigned long long TOR_INSERTS_MISS_OPCODE;
    unsigned long long DRD_OPCODE_FILTER;

    /* The old and current TOR OCCUPANCY, TOR_INSERTS, and latency */
    uint64_t oldoc, oldin,
             curoc, curin,
             curlat, oldlat;

  public:
    /* Start/Stop the profiler */
    static void engage()        PRODUCT_RETURN;
    static void disengage()     PRODUCT_RETURN;
    static bool is_active()     PRODUCT_RETURN0;

    /* Constructor */
    LatencySampler()
    {
      C0_CTL = strtoul("0x0D04", NULL, 0); /* The whole box's control register */
      C0_CTL0 = strtoul("0x0D10", NULL, 0); /* The control registers */
      C0_CTL1 = strtoul("0x0D11", NULL, 0);
      C0_CTR0 = strtoul("0x0D16", NULL, 0); /* The counter registers */
      C0_CTR1 = strtoul("0x0D17", NULL, 0);
      C0_FILTER1 = strtoul("0x0D1A", NULL, 0); /* The filter register */

      /* The register values */
      BOX_RESET_AND_FREEZE = strtoull("0x10102", NULL, 0);
      BOX_UNFREEZE = strtoull("0x10000", NULL, 0);
      TOR_OCCUPANCY_MISS_OPCODE = strtoull("0x400336", NULL, 0);
      TOR_INSERTS_MISS_OPCODE = strtoull("0x400335", NULL, 0);
      DRD_OPCODE_FILTER = strtoull("0x18200000", NULL, 0);
    }
};


#endif // SHARE_VM_RUNTIME_LATENCYSAMPLER_HPP
#endif

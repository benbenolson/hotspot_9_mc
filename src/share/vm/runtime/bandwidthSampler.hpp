#ifndef SHARE_VM_RUNTIME_BANDWIDTHSAMPLER_HPP
#define SHARE_VM_RUNTIME_BANDWIDTHSAMPLER_HPP

//#include <unistd.h>
//#include <iostream>
//#include <sys/time.h>
//#include <fcntl.h>
//#include <cstdio>
//#include <cstring>
//#include <cmath>
#include <map>
//#include <vector>
#include "utilities/growableArray.hpp"
//#include <sstream>
//#include <stdlib.h>
#include <sys/mman.h>
#include <string>
//#include <iomanip>
#include "runtime/vm_operations.hpp"

/* Samples the bandwidth on the system in an attempt
 * to reduce the bandwidth bottleneck caused by our
 * power-saving efforts
 */

typedef unsigned long long uint64;
typedef signed long long int64;
typedef unsigned int uint32;
typedef signed int int32;

class PCM;
class ServerUncorePowerState;
class CoreCounterState;
class BasicCounterState;

/************************************
 *           GLOBALS                *
 ***********************************/

#ifndef INTELPCM_API
#define INTELPCM_API
#endif

#define PLATFORM_INFO_ADDR (0xCE)
#define PCM_CALIBRATION_INTERVAL 50


#define READ 0
#define WRITE 1
#define PARTIAL 2

#define JKTIVT_MC0_CH0_REGISTER_DEV_ADDR (16)
#define JKTIVT_MC0_CH1_REGISTER_DEV_ADDR (16)
#define JKTIVT_MC0_CH2_REGISTER_DEV_ADDR (16)
#define JKTIVT_MC0_CH3_REGISTER_DEV_ADDR (16)
#define JKTIVT_MC0_CH0_REGISTER_FUNC_ADDR (4)
#define JKTIVT_MC0_CH1_REGISTER_FUNC_ADDR (5)
#define JKTIVT_MC0_CH2_REGISTER_FUNC_ADDR (0)
#define JKTIVT_MC0_CH3_REGISTER_FUNC_ADDR (1)

#define JKTIVT_MC1_CH0_REGISTER_DEV_ADDR (30)
#define JKTIVT_MC1_CH1_REGISTER_DEV_ADDR (30)
#define JKTIVT_MC1_CH2_REGISTER_DEV_ADDR (30)
#define JKTIVT_MC1_CH3_REGISTER_DEV_ADDR (30)
#define JKTIVT_MC1_CH0_REGISTER_FUNC_ADDR (4)
#define JKTIVT_MC1_CH1_REGISTER_FUNC_ADDR (5)
#define JKTIVT_MC1_CH2_REGISTER_FUNC_ADDR (0)
#define JKTIVT_MC1_CH3_REGISTER_FUNC_ADDR (1)

#define HSX_MC0_CH0_REGISTER_DEV_ADDR (20)
#define HSX_MC0_CH1_REGISTER_DEV_ADDR (20)
#define HSX_MC0_CH2_REGISTER_DEV_ADDR (21)
#define HSX_MC0_CH3_REGISTER_DEV_ADDR (21)
#define HSX_MC0_CH0_REGISTER_FUNC_ADDR (0)
#define HSX_MC0_CH1_REGISTER_FUNC_ADDR (1)
#define HSX_MC0_CH2_REGISTER_FUNC_ADDR (0)
#define HSX_MC0_CH3_REGISTER_FUNC_ADDR (1)

#define HSX_MC1_CH0_REGISTER_DEV_ADDR (23)
#define HSX_MC1_CH1_REGISTER_DEV_ADDR (23)
#define HSX_MC1_CH2_REGISTER_DEV_ADDR (24)
#define HSX_MC1_CH3_REGISTER_DEV_ADDR (24)
#define HSX_MC1_CH0_REGISTER_FUNC_ADDR (0)
#define HSX_MC1_CH1_REGISTER_FUNC_ADDR (1)
#define HSX_MC1_CH2_REGISTER_FUNC_ADDR (0)
#define HSX_MC1_CH3_REGISTER_FUNC_ADDR (1)

#define PCM_PCI_VENDOR_ID_OFFSET (0)
#define PCM_INTEL_PCI_VENDOR_ID (0x8086)

#define PciHandleM PciHandleMM

struct INTELPCM_API TopologyEntry // decribes a core
{
  int32 os_id;
  int32 socket;
  int32 core_id;
  
  TopologyEntry() : os_id(-1), socket(-1), core_id(-1) {}
};

/**************************************
 *          MSRHANDLE                 *
 *************************************/
class MsrHandle
{
  int32 fd;
  uint32 cpu_id;
  public:
    MsrHandle(uint32 cpu);
    int32 read(uint64 msr_number, uint64 * value);
};

class SafeMsrHandle
{
  MsrHandle * pHandle;
  public:
    SafeMsrHandle() : pHandle(NULL) {}
    SafeMsrHandle(uint32 core_id)
    {
      pHandle = new MsrHandle(core_id);
    }
    int32 read(uint64 msr_number, uint64 * value)
    {
      if(pHandle)
        return pHandle->read(msr_number, value);
      *value = 0;
      return sizeof(uint64);
    }
};

/********************************
 *     CORECOUNTERSTATE         *
 *******************************/
class BasicCounterState
{
  public:
    uint64 InvariantTSC;
    void readAndAggregate(SafeMsrHandle*);

    BasicCounterState() :
      InvariantTSC(0)
    {
    }
};

//! \brief (Logical) core-wide counter state
class CoreCounterState : public BasicCounterState
{
  friend class PCM;
};

/**********************************
 *               MCFG             *
 *********************************/
struct MCFGRecord
{   
  unsigned long long baseAddress;
  unsigned short PCISegmentGroupNumber;
  unsigned char startBusNumber;
  unsigned char endBusNumber;
  char reserved[4];
};

struct MCFGHeader
{
  char signature[4];
  unsigned length;
  unsigned char revision;
  unsigned char checksum;
  char OEMID[6];
  char OEMTableID[8];
  unsigned OEMRevision;
  unsigned creatorID;
  unsigned creatorRevision;
  char reserved[8];

  unsigned nrecords() const
  {
    return (length - sizeof(MCFGHeader))/sizeof(MCFGRecord);
  }
};

/**********************************
*         PciHandleM(M)           *
**********************************/
// read/write PCI config space using physical memory using mmaped file I/O
class PciHandleMM
{
  int32 fd;
  char * mmapAddr;
  
  uint32 bus;
  uint32 device;
  uint32 function;
  uint64 base_addr; 

  static MCFGHeader mcfgHeader;
  //static std::vector<MCFGRecord> mcfgRecords;
  static GrowableArray<MCFGRecord*>* mcfgRecords;
  static void readMCFG();

  public:
    //static const std::vector<MCFGRecord> & getMCFGRecords();
    static const GrowableArray<MCFGRecord*>* getMCFGRecords();
    int32 read64(uint64 offset, uint64 * value);
    int32 read32(uint64 offset, uint32 * value);
    static bool exists(uint32 bus_, uint32 device_, uint32 function_);
    PciHandleMM(uint32 groupnr_, uint32 bus_, uint32 device_, uint32 function_);
};


/***********************************
 *      ServerPCICFGUncore         *
 **********************************/
#define MC_CH_PCI_PMON_CTR3_ADDR (0x0B8)
#define MC_CH_PCI_PMON_CTR2_ADDR (0x0B0)
#define MC_CH_PCI_PMON_CTR1_ADDR (0x0A8)
#define MC_CH_PCI_PMON_CTR0_ADDR (0x0A0)

static const uint32 IMC_DEV_IDS[] = {
  0x03cb0,
  0x03cb1,
  0x03cb4,
  0x03cb5,
  0x0EB4,
  0x0EB5,
  0x0EB0,
  0x0EB1,
  0x0EF4,
  0x0EF5,
  0x0EF0,
  0x0EF1,
  0x2fb0,
  0x2fb1,
  0x2fb4,
  0x2fb5,
  0x2fd0,
  0x2fd1,
  0x2fd4,
  0x2fd5
};


class ServerPCICFGUncore
{
  int bus, groupnr;
  PciHandleM ** imcHandles;
  uint32 num_imc_channels;
  //static std::vector<std::pair<uint32,uint32> > socket2bus;
  static GrowableArray< std::pair<uint32, uint32>* >* socket2bus;
  void initSocket2Bus();

  uint32 MCX_CHY_REGISTER_DEV_ADDR[2][4];
  uint32 MCX_CHY_REGISTER_FUNC_ADDR[2][4];

  public:
    uint32 getNumMCChannels() const { 
      return num_imc_channels;
    }
    ServerPCICFGUncore(uint32 socket_, PCM * pcm);
    PciHandleM * createIntelPerfMonDevice(uint32 groupnr_, uint32 bus_, uint32 dev_, uint32 func_, bool checkVendor);
    uint64 getMCCounter(uint32 channel, uint32 counter);
};

/*********************************
 *            PCM                *
 ********************************/
union PCM_CPUID_INFO
{
  int array[4];
  struct { int eax, ebx, ecx, edx; } reg;
};

class INTELPCM_API PCM
{
  int32 cpu_model;
 // std::vector<TopologyEntry> topology;
  TopologyEntry topology[24];
  ServerPCICFGUncore ** server_pcicfg_uncore;
  static PCM * instance;
  uint64 nominal_frequency;
  int32 num_sockets;
  int32 num_cores;
  int32 num_phys_cores_per_socket;
  SafeMsrHandle ** MSR;
  //std::vector<int32> socketRefCore;
  int32 *socketRefCore;

  public:
    enum SupportedCPUModels
    {
      NEHALEM_EP = 26,
      NEHALEM = 30,
      ATOM = 28,
      ATOM_2 = 53,
      ATOM_CENTERTON = 54,
      ATOM_BAYTRAIL = 55,
      ATOM_AVOTON = 77,
      CLARKDALE = 37,
      WESTMERE_EP = 44,
      NEHALEM_EX = 46,
      WESTMERE_EX = 47,
      SANDY_BRIDGE = 42,
      JAKETOWN = 45,
      IVY_BRIDGE = 58,
      HASWELL = 60,
      HASWELL_ULT = 69,
      HASWELL_2 = 70,
      IVYTOWN = 62,
      HASWELLX = 63,
      END_OF_MODEL_LIST = 0x0ffff
    };

    static std::string getCPUBrandString();
    uint32 getCPUModel() { return cpu_model; }
    uint32 getNumSockets();
    uint64 getTickCount();
    CoreCounterState getCoreCounterState(uint32 core);
    bool detectModel();
    bool detectNominalFrequency();
    bool discoverSystemTopology();
    void initMSR();
    bool isCoreOnline(int32 os_core_id) const;
    ServerUncorePowerState getServerUncorePowerState(uint32 socket);
    uint64 getNominalFrequency();
    static PCM * getInstance();
    void initUncoreObjects();

    PCM();
};

/**********************************
 *     ServerUncorePowerState     *
 *********************************/
class ServerUncorePowerState
{
  public:
    uint64 MCCounter[8][4];
};

/**********************************
 *      BANDWIDTHSAMPLER          *
 *********************************/
class BandwidthSamplerTask;

class BandwidthSampler : AllStatic {
  friend class BandwidthSamplerTask;
  friend class SamplerThread;
private:

  // The task
  static BandwidthSamplerTask* _task;
public:
  //Start/stop the profiler
  static void engage()        PRODUCT_RETURN;
  static void disengage()     PRODUCT_RETURN;
  static bool is_active()     PRODUCT_RETURN0;
};
#endif // SHARE_VM_RUNTIME_BANDWIDTHSAMPLER_HPP

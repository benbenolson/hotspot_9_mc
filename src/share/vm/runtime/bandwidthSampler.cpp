#include "precompiled.hpp"
#include "classfile/systemDictionary.hpp"
#include "code/codeCache.hpp"
#include "gc/shared/collectedHeap.inline.hpp"
#include "interpreter/oopMapCache.hpp"
#include "memory/resourceArea.hpp"
#include "runtime/handles.inline.hpp"
#include "runtime/jniHandles.hpp"
#include "runtime/memprofiler.hpp"
#include "runtime/hotMethodSampler.hpp"
#include "runtime/mutexLocker.hpp"
#include "runtime/os.hpp"
#include "runtime/task.hpp"
#include "runtime/vmThread.hpp"
#include "code/nmethod.hpp"
#include "oops/oop.hpp"
#include "runtime/thread.hpp"
#include "runtime/frame.hpp"
#include "runtime/vframe.hpp"
#include "runtime/vm_operations.hpp"
#include "runtime/mutexLocker.hpp"
#include "interpreter/interpreterRuntime.hpp"
#include "runtime/os.hpp"
#include "memory/heapInspection.hpp"
#include "memory/resourceArea.hpp"
#include "oops/klass.hpp"
#include "runtime/os.hpp"
#include "utilities/globalDefinitions.hpp"
#include "runtime/mutexLocker.hpp"
#include "runtime/bandwidthSampler.hpp"
#ifndef SERIALGC
#include "gc/parallel/parallelScavengeHeap.hpp"
#endif
#include "setjmp.h"

#ifdef TARGET_OS_FAMILY_linux
# include "thread_linux.inline.hpp"
#endif
#ifdef TARGET_OS_FAMILY_solaris
# include "thread_solaris.inline.hpp"
#endif
#ifdef TARGET_OS_FAMILY_windows
# include "thread_windows.inline.hpp"
#endif

#ifndef PRODUCT

/*********************************
 *            PCM                *
 ********************************/
PCM * PCM::getInstance()
{
  if (instance) return instance;

  return instance = new PCM();
}

void pcm_cpuid(int leaf, PCM_CPUID_INFO & info)
{
  __asm__ __volatile__ ("cpuid" : \
                        "=a" (info.reg.eax), \
                        "=b" (info.reg.ebx), \
                        "=c" (info.reg.ecx), \
                        "=d" (info.reg.edx) : \
                        "a" (leaf));
}

/*
std::string PCM::getCPUBrandString()
{
  char buffer[sizeof(int)*4*3+1];
  size_t i = std::string::npos;

  PCM_CPUID_INFO * info = (PCM_CPUID_INFO *) buffer;
  pcm_cpuid(0x80000002, *info);
  ++info;
  pcm_cpuid(0x80000003, *info);
  ++info;
  pcm_cpuid(0x80000004, *info);
  buffer[sizeof(int)*4*3] = 0;
  std::string result(buffer);
  while(result[0]==' ') result.erase(0,1);
  while((i=result.find("  "))!=std::string::npos) result.replace(i,2," "); // remove duplicate spaces
  return result;
}
*/

void PCM::initUncoreObjects()
{
  if (MSR != NULL) {
    server_pcicfg_uncore = new ServerPCICFGUncore *[num_sockets];
    int i = 0;
    for (i = 0; i < num_sockets; ++i) {
      server_pcicfg_uncore[i] = new ServerPCICFGUncore(i, this);
    }
  }
}


bool PCM::detectModel()
{
  PCM_CPUID_INFO cpuinfo;
  pcm_cpuid(0, cpuinfo);
  pcm_cpuid(1, cpuinfo);
  cpu_model = (((cpuinfo.array[0]) & 0xf0) >> 4) | ((cpuinfo.array[0] & 0xf0000) >> 12);

  return true;
}

ServerUncorePowerState PCM::getServerUncorePowerState(uint32 socket)
{
  ServerUncorePowerState result;
  if(server_pcicfg_uncore && server_pcicfg_uncore[socket]) {  
    for(uint32 channel=0; channel < server_pcicfg_uncore[socket]->getNumMCChannels(); ++channel)
    {
      for(uint32 cnt = 0; cnt < 4; ++cnt) {
        result.MCCounter[channel][cnt] = server_pcicfg_uncore[socket]->getMCCounter(channel, cnt);
      }
    }
  }
  return result;
}

uint64 PCM::getNominalFrequency()
{
  return nominal_frequency;
}

CoreCounterState PCM::getCoreCounterState(uint32 core)
{
  CoreCounterState result;
  if(MSR) result.readAndAggregate(MSR[core]);
  return result;
}

uint64 getInvariantTSC(const CoreCounterState & before, const CoreCounterState & after)
{
  return after.InvariantTSC - before.InvariantTSC;
}

uint64 PCM::getTickCount()
{
  uint64 multiplier = 1000;
  uint32 core = 0;
  uint64 invariant = getInvariantTSC(CoreCounterState(), getCoreCounterState(core));
  return (multiplier * invariant) / getNominalFrequency();
}

bool PCM::discoverSystemTopology()
{
  ResourceMark rm;
  GrowableArray<uint32>* socketIdMap = new GrowableArray<uint32>(1);
  int32 socket;
  char buffer[1024];
  TopologyEntry entry;
  int32 i = 0;

  /* Get num_cores */
  FILE * f_presentcpus = fopen("/sys/devices/system/cpu/present", "r");
  if (!f_presentcpus)
  {
      fprintf(stderr, "Can not open /sys/devices/system/cpu/present file.\n");
      return false;
  }
  if(NULL == fgets(buffer, 1024, f_presentcpus))
  {
      fprintf(stderr, "Can not read /sys/devices/system/cpu/present.\n");
      return false;
  }
  fclose(f_presentcpus);
  num_cores = -1;
  sscanf(buffer, "0-%d", &num_cores);
  if(num_cores == -1)
  {
    sscanf(buffer, "%d", &num_cores);
  }
  if(num_cores == -1)
  {
    fprintf(stderr, "Can not read number of present cores\n");
    return false;
  }
  ++num_cores;
  
  /* Get num_sockets */
  FILE * f_cpuinfo = fopen("/proc/cpuinfo", "r");
  if (!f_cpuinfo) {
      fprintf(stderr, "Can not open /proc/cpuinfo file.\n");
      return false;
  }
  
  /*
  topology.resize(num_cores);
  while (0 != fgets(buffer, 1024, f_cpuinfo)) {
    if (strncmp(buffer, "processor", sizeof("processor") - 1) == 0) {
      if (entry.os_id >= 0) {
        topology[entry.os_id] = entry;
      }
      sscanf(buffer, "processor\t: %d", &entry.os_id);
      continue;
    }
    if (strncmp(buffer, "physical id", sizeof("physical id") - 1) == 0) {
      sscanf(buffer, "physical id\t: %d", &entry.socket);
      socketIdMap[entry.socket] = 0;
      continue;
    }
    if (strncmp(buffer, "core id", sizeof("core id") - 1) == 0) {
      sscanf(buffer, "core id\t: %d", &entry.core_id);
      continue;
    }
    if (strncmp(buffer, "cpu cores", sizeof("cpu cores") - 1) == 0) {
      sscanf(buffer, "cpu cores\t: %d", &num_phys_cores_per_socket);
      continue;
    }
    if (entry.os_id >= 0) {
      topology[entry.os_id] = entry;
    }
  }
  */
  printf("Opening cpuinfo\n");
  fflush(stdout);
  while (0 != fgets(buffer, 1024, f_cpuinfo)) {
    printf("Reading a line.\n");
    fflush(stdout);
    if (strncmp(buffer, "processor", sizeof("processor") - 1) == 0) {
      if (entry.os_id >= 0) {
        printf("Putting a new processor.\n");
        fflush(stdout);
        topology[entry.os_id] = entry;
      }
      sscanf(buffer, "processor\t: %d", &entry.os_id);
      continue;
    }
    if (strncmp(buffer, "physical id", sizeof("physical id") - 1) == 0) {
      sscanf(buffer, "physical id\t: %d", &entry.socket);
      if(socketIdMap->find(entry.socket) == -1) {
        printf("Putting a new socketIdMap entry.\n");
        fflush(stdout);
        socketIdMap->at_put_grow(i, entry.socket, 0);
        ++i;
      }
      continue;
    }
    if (strncmp(buffer, "core id", sizeof("core id") - 1) == 0) {
      sscanf(buffer, "core id\t: %d", &entry.core_id);
      continue;
    }
    if (strncmp(buffer, "cpu cores", sizeof("cpu cores") - 1) == 0) {
      sscanf(buffer, "cpu cores\t: %d", &num_phys_cores_per_socket);
      continue;
    }
    if (entry.os_id >= 0) {
      printf("Putting the entry in topology\n");
      topology[entry.os_id] = entry;
    }
  }
  if(socketIdMap->length() < 1) {
    num_sockets = 1;
  } else {
    num_sockets = socketIdMap->length();
  }
  fclose(f_cpuinfo);

  /*
  socketRefCore.resize(num_sockets);
  */
  socketRefCore = (int32 *) malloc(sizeof(int32) * num_sockets);
  for(i = 0; i < num_cores; ++i) {
    if(isCoreOnline(i)) {
      socketRefCore[topology[i].socket] = i;
    }
  }
  
  printf("Finished growing socketRefCore.\n");
  return true;
}

bool PCM::isCoreOnline(int32 os_core_id) const
{
  return (topology[os_core_id].os_id != -1) && (topology[os_core_id].core_id != -1) && (topology[os_core_id].socket != -1);
  return true;
}


void PCM::initMSR()
{
  int32 i = 0;
  MSR = new SafeMsrHandle *[num_cores];
  for(i = 0; i < num_cores; ++i) {
    MSR[i] = isCoreOnline(i) ? (new SafeMsrHandle(i)):(new SafeMsrHandle());
  }
}

PCM::PCM() :
  cpu_model(-1)
{
  uint32 retval = 0;
  
  printf("Initializing PCM\n");
  fflush(stdout);
  if(!detectModel()) return;
  printf("Finished detecting model.\n");
  fflush(stdout);
  if(!discoverSystemTopology()) return;
  printf("Finished discovering system topology.\n");
  fflush(stdout);
  initMSR();
  printf("Finished initializing MSR.\n");
  fflush(stdout);
  if(!detectNominalFrequency()) return;
  printf("Finished getting nominal frequency.\n");
  fflush(stdout);
  initUncoreObjects();
}

uint32 PCM::getNumSockets()
{
  return num_sockets;
}

/*
uint64 get_frequency_from_cpuid()
{
  double speed=0;
  std::string brand = PCM::getCPUBrandString();
  if(brand.length() > 0)
  {
    size_t unitsg = brand.find("GHz");
    if(unitsg != std::string::npos) {
      size_t atsign = brand.rfind(' ', unitsg);
      if(atsign != std::string::npos) {
        std::istringstream(brand.substr(atsign)) >> speed;
        speed *= 1000;
      }
    } else {
      size_t unitsg = brand.find("MHz");
      if(unitsg != std::string::npos) {
        size_t atsign = brand.rfind(' ', unitsg);
        if(atsign != std::string::npos) {
          std::istringstream(brand.substr(atsign)) >> speed;
        }
      }
    }
  }
  return (uint64)speed * 1000ULL * 1000ULL;
}
*/

bool PCM::detectNominalFrequency()
{
  if(MSR) {
    uint64 freq = 0;
    MSR[socketRefCore[0]]->read(PLATFORM_INFO_ADDR, &freq);
    const uint64 bus_freq = (
              cpu_model == SANDY_BRIDGE
           || cpu_model == JAKETOWN
           || cpu_model == IVYTOWN
           || cpu_model == HASWELLX
           || cpu_model == IVY_BRIDGE
           || cpu_model == HASWELL
           ) ? (100000000ULL) : (133333333ULL);
    
    nominal_frequency = ((freq >> 8) & 255) * bus_freq;
    
    /*
    if(!nominal_frequency)
      nominal_frequency = get_frequency_from_cpuid();
    */
    if(!nominal_frequency) {
      fprintf(stderr, "Error: Can not detect core frequency. freq = %Lu, bus_freq = %Lu\n", freq, bus_freq);
      return false;
    }
  }

  return true;
}

/**********************************
*         PciHandleM(M)           *
**********************************/
int32 PciHandleMM::read64(uint64 offset, uint64 * value)
{
  read32(offset, (uint32 *)value);
  read32(offset + sizeof(uint32), ((uint32 *)value) + 1);

  return sizeof(uint64);
}

int32 PciHandleMM::read32(uint64 offset, uint32 * value)
{
  *value = *((uint32*)(mmapAddr+offset));

  return sizeof(uint32);
}

PciHandleMM::PciHandleMM(uint32 groupnr_, uint32 bus_, uint32 device_, uint32 function_) :
  fd(-1),
  mmapAddr(NULL),
  bus(bus_),
  device(device_),
  function(function_),
  base_addr(0)
{
  int handle = ::open("/dev/mem", O_RDWR);
  if (handle < 0) {
    fprintf(stderr, "Can't mmap things.\n");
    exit(1);
  }
  fd = handle;

  readMCFG();

  unsigned segment = 0;
  /*
  for(; segment < mcfgRecords.size(); ++segment)
  {
    if(   mcfgRecords[segment].PCISegmentGroupNumber == groupnr_
      &&  mcfgRecords[segment].startBusNumber <= bus_
      &&  bus <= mcfgRecords[segment].endBusNumber)
      break;
  }
  if(segment == mcfgRecords.size()) {
    fprintf(stderr, "PCM Error: (group %u, bus %u) not found in the MCFG table.\n", groupnr_, bus_);
    exit(1);
  }


  base_addr = mcfgRecords[segment].baseAddress;
  */
  base_addr += (bus * 1024 * 1024 + device * 32 * 1024 + function * 4 * 1024);
  mmapAddr = (char*) mmap(NULL, 4096, PROT_READ| PROT_WRITE, MAP_SHARED , fd, base_addr);

  if(mmapAddr == MAP_FAILED) {
    fprintf(stderr, "Failed to mmap things.\n");
    exit(1);
  }

}

MCFGHeader PciHandleMM::mcfgHeader;
//std::vector<MCFGRecord> PciHandleMM::mcfgRecords;
GrowableArray<MCFGRecord*>* PciHandleMM::mcfgRecords = NULL;

/*
const std::vector<MCFGRecord> & PciHandleMM::getMCFGRecords()
{
  readMCFG();
  return mcfgRecords;
}          
*/
const GrowableArray<MCFGRecord*>* PciHandleMM::getMCFGRecords()
{
  readMCFG();
  return mcfgRecords;
}

void PciHandleMM::readMCFG()
{
  /*
  if(mcfgRecords.size() > 0 )
    return; // already initialized
    */
  if(mcfgRecords != NULL)
    return;

  mcfgRecords = new GrowableArray<MCFGRecord*>(1);

  const char * path = "/sys/firmware/acpi/tables/MCFG";
  int mcfg_handle = ::open(path, O_RDONLY);

  if (mcfg_handle < 0) {
    fprintf(stderr, "PCM Error: Cannot open \n");
    exit(1);
  }

  ssize_t read_bytes = ::read(mcfg_handle, (void *)&mcfgHeader, sizeof(MCFGHeader));

  if(read_bytes == 0) {
    fprintf(stderr, "PCM Error: Cannot read\n");
    exit(1);
  }

  const unsigned segments = mcfgHeader.nrecords();

  for(unsigned int i=0; i<segments;++i) {
    MCFGRecord *record = new MCFGRecord;
    read_bytes = ::read(mcfg_handle, (void *)record, sizeof(MCFGRecord));
    if(read_bytes == 0) {
      fprintf(stderr, "PCM Error: Cannot read \n");
      exit(1);
    }
    /*
    mcfgRecords.push_back(record);
    */
    mcfgRecords->append(record);
    //record.print();
  }
  
  ::close(mcfg_handle);
}

PciHandleMM * ServerPCICFGUncore::createIntelPerfMonDevice(uint32 groupnr_, uint32 bus_, uint32 dev_, uint32 func_, bool checkVendor)
{
  if (PciHandleMM::exists(bus_, dev_, func_)) {
    PciHandleMM * handle = new PciHandleMM(groupnr_, bus_, dev_, func_);

    if(!checkVendor) return handle;
    
    uint32 vendor_id = 0;
    handle->read32(PCM_PCI_VENDOR_ID_OFFSET,&vendor_id);
    vendor_id &= 0x0ffff;

    if(vendor_id == PCM_INTEL_PCI_VENDOR_ID) return handle;

    delete handle;
  }
  return NULL;
}

bool PciHandleMM::exists(uint32 bus_, uint32 device_, uint32 function_)
{
  int handle = ::open("/dev/mem", O_RDWR);
  if (handle < 0) return false;
  ::close(handle);

  handle = ::open("/sys/firmware/acpi/tables/MCFG", O_RDONLY);
  if (handle < 0) return false;
  ::close(handle);

  return true;
}

/***********************************
 *      ServerPCICFGUncore         *
 **********************************/
//std::vector<std::pair<uint32,uint32> > ServerPCICFGUncore::socket2bus;
GrowableArray< std::pair<uint32, uint32>* >* ServerPCICFGUncore::socket2bus = NULL;

void ServerPCICFGUncore::initSocket2Bus()
{
  //if(!socket2bus.empty()) return;
  if(socket2bus != NULL) return;

  //const std::vector<MCFGRecord> & mcfg = PciHandleMM::getMCFGRecords();
  const GrowableArray<MCFGRecord*>* mcfg = PciHandleMM::getMCFGRecords();
  /*
  for(uint32 s = 0; s < mcfg.size(); ++s)
  for(uint32 bus = mcfg[s].startBusNumber; bus <= mcfg[s].endBusNumber; ++bus) {
    uint32 value = 0;
      PciHandleMM h(mcfg[s].PCISegmentGroupNumber, bus, MCX_CHY_REGISTER_DEV_ADDR[0][0], MCX_CHY_REGISTER_FUNC_ADDR[0][0]);
      h.read32(0, &value);
    const uint32 vendor_id = value & 0xffff;
    const uint32 device_id = (value >> 16) & 0xffff;
    if (vendor_id != PCM_INTEL_PCI_VENDOR_ID)
       continue;

    for(uint32 i = 0; i< sizeof(IMC_DEV_IDS)/sizeof(IMC_DEV_IDS[0]) ; ++i) {
      // match
      if(IMC_DEV_IDS[i] == device_id) {
        socket2bus.push_back(std::make_pair(mcfg[s].PCISegmentGroupNumber,bus));
        break;
      }
    }
  }
  */
  /*
  for(uint32 s = 0; s < mcfg->length(); ++s) {
    for(uint32 bus = mcfg->at(s).startBusNumber; bus <= mcfg->at(s).endBusNumber; ++bus) {
      uint32 value = 0;
      PciHandleMM h(mcfg->at(s).PCISegmentGroupNumber, bus, MCX_CHY_REGISTER_DEV_ADDR[0][0], MCX_CHY_REGISTER_FUNC_ADDR[0][0]);
      h.read32(0, &value);
      const uint32 vendor_id = value & 0xffff;
      const uint32 device_id = (value >> 16) & 0xffff;
      if(vendor_id != PCM_INTEL_PCI_VENDOR_ID)
        continue;
      for(uint32 i = 0; i < sizeof(IMC_DEV_IDS)/sizeof(IMC_DEV_IDS[0]); ++i) {
        if(IMC_DEV_IDS[i] == device_id) {
          socket2bus->append(std::make_pair(mcfg->at(s).PCISegmentGroupNumber, bus));
          break;
        }
      }
    }
  }
  */
}

int getBusFromSocket(const uint32 socket)
{
  int cur_bus = 0;
  uint32 cur_socket = 0;
  while(cur_socket <= socket) {
    PciHandleMM h(0, cur_bus, 5, 0);
    uint32 cpubusno = 0;
    h.read32(0x108, &cpubusno); // CPUBUSNO register
    cur_bus = (cpubusno >> 8)& 0x0ff;
    if(socket == cur_socket)
      return cur_bus;
    ++cur_socket;
    ++cur_bus;
    if(cur_bus > 0x0ff)
       return -1;
  }

  return -1;
}

ServerPCICFGUncore::ServerPCICFGUncore(uint32 socket_, PCM * pcm) :
     bus(-1)
   , groupnr(0)
   , imcHandles(NULL)
   , num_imc_channels(0)
{
  #define PCM_PCICFG_MC_INIT(controller, channel, arch) \
      MCX_CHY_REGISTER_DEV_ADDR[controller][channel] = arch##_MC##controller##_CH##channel##_REGISTER_DEV_ADDR; \
      MCX_CHY_REGISTER_FUNC_ADDR[controller][channel] = arch##_MC##controller##_CH##channel##_REGISTER_FUNC_ADDR;
  
  const uint32 cpu_model = pcm->getCPUModel();

  if(cpu_model == PCM::JAKETOWN || cpu_model == PCM::IVYTOWN)
  {
    PCM_PCICFG_MC_INIT(0, 0, JKTIVT)
    PCM_PCICFG_MC_INIT(0, 1, JKTIVT)
    PCM_PCICFG_MC_INIT(0, 2, JKTIVT)
    PCM_PCICFG_MC_INIT(0, 3, JKTIVT)
    PCM_PCICFG_MC_INIT(1, 0, JKTIVT)
    PCM_PCICFG_MC_INIT(1, 1, JKTIVT)
    PCM_PCICFG_MC_INIT(1, 2, JKTIVT)
    PCM_PCICFG_MC_INIT(1, 3, JKTIVT)
  }
  else if(cpu_model == PCM::HASWELLX)
  {
    PCM_PCICFG_MC_INIT(0, 0, HSX)
    PCM_PCICFG_MC_INIT(0, 1, HSX)
    PCM_PCICFG_MC_INIT(0, 2, HSX)
    PCM_PCICFG_MC_INIT(0, 3, HSX)
    PCM_PCICFG_MC_INIT(1, 0, HSX)
    PCM_PCICFG_MC_INIT(1, 1, HSX)
    PCM_PCICFG_MC_INIT(1, 2, HSX)
    PCM_PCICFG_MC_INIT(1, 3, HSX)
  }
  else
  {
    fprintf(stderr, "Not using the right processor types.\n");
    exit(1);
  }

  initSocket2Bus();
  const uint32 total_sockets_ = pcm->getNumSockets();
  
  /*
  if(total_sockets_ == socket2bus.size())
  {
    groupnr = socket2bus[socket_].first;
    bus = socket2bus[socket_].second;
  } else if(total_sockets_ <= 4)
  {
    bus = getBusFromSocket(socket_);
    if(bus < 0)
    {
      exit(1);
    }
  }
  else
  {
    exit(1);
  }
  */
  
  imcHandles = new PciHandleMM * [8];
  
  #define PCM_PCICFG_SETUP_MC_HANDLE(controller,channel)        \
  {                                                             \
    PciHandleMM * handle = createIntelPerfMonDevice(groupnr, bus,\
                MCX_CHY_REGISTER_DEV_ADDR[controller][channel], \
                MCX_CHY_REGISTER_FUNC_ADDR[controller][channel],\
                true);                                          \
    if(handle) imcHandles[num_imc_channels++] = handle;         \
  }

  PciHandleMM * handle = createIntelPerfMonDevice(groupnr, bus,
              MCX_CHY_REGISTER_DEV_ADDR[0][0], 
              MCX_CHY_REGISTER_FUNC_ADDR[0][0],
              true);                                          
  if(handle) imcHandles[num_imc_channels++] = handle;
  PCM_PCICFG_SETUP_MC_HANDLE(0,0)
  PCM_PCICFG_SETUP_MC_HANDLE(0,1)
  PCM_PCICFG_SETUP_MC_HANDLE(0,2)
  PCM_PCICFG_SETUP_MC_HANDLE(0,3)

  PCM_PCICFG_SETUP_MC_HANDLE(1,0)
  PCM_PCICFG_SETUP_MC_HANDLE(1,1)
  PCM_PCICFG_SETUP_MC_HANDLE(1,2)
  PCM_PCICFG_SETUP_MC_HANDLE(1,3)

}

uint64 ServerPCICFGUncore::getMCCounter(uint32 channel, uint32 counter)
{
  uint64 result = 0;
  
  if(channel < num_imc_channels) {
    switch(counter) {
      case 0:
        imcHandles[channel]->read64(MC_CH_PCI_PMON_CTR0_ADDR, &result);
        break;
      case 1:
        imcHandles[channel]->read64(MC_CH_PCI_PMON_CTR1_ADDR, &result);
        break;
      case 2:
        imcHandles[channel]->read64(MC_CH_PCI_PMON_CTR2_ADDR, &result);
        break;
      case 3:
        imcHandles[channel]->read64(MC_CH_PCI_PMON_CTR3_ADDR, &result);
        break;
    }
  }
  
  return result;
}

/****************************
 *      CORECOUNTERSTATE    *
 ***************************/
void BasicCounterState::readAndAggregate(SafeMsrHandle *msr)
{
  uint64 cInvariantTSC = 0;
  PCM *m = PCM::getInstance();
    
  struct timeval tp;
  gettimeofday(&tp, NULL);
  cInvariantTSC = (double(tp.tv_sec) + tp.tv_usec / 1000000.)*m->getNominalFrequency();

  InvariantTSC += cInvariantTSC;
}

CoreCounterState getCoreCounterState(uint32 core)
{
  PCM * inst = PCM::getInstance();
  CoreCounterState result;
  if (inst) result = inst->getCoreCounterState(core);
  return result;
}


/**************************************
 *          MSRHANDLE                 *
 *************************************/
int32 MsrHandle::read(uint64 msr_number, uint64 * value)
{
  return ::pread(fd, (void *)value, sizeof(uint64), msr_number);
}

MsrHandle::MsrHandle(uint32 cpu) :
  fd(-1),
  cpu_id(cpu)
{
  char * path = new char[200];
  sprintf(path, "/dev/cpu/%d/msr", cpu);
  int handle = ::open(path, O_RDWR);
  if(handle < 0)
  { // try Android msr device path
    sprintf(path, "/dev/msr%d", cpu);
    handle = ::open(path, O_RDWR);
  }
  delete[] path;
  if (handle < 0) {
    fprintf(stderr, "Can't get msrhandle.\n");
    exit(1);
  }
  fd = handle;
}

uint64 getMCCounter(uint32 channel, uint32 counter, const ServerUncorePowerState & before, const ServerUncorePowerState & after)
{
  return after.MCCounter[channel][counter] - before.MCCounter[channel][counter];
}

/****************************
*      DISPLAY_BANDWIDTH    *
****************************/
void display_bandwidth(float *iMC_Rd_socket_chan, float *iMC_Wr_socket_chan, float *iMC_Rd_socket, float *iMC_Wr_socket, uint32 numSockets, uint32 num_imc_channels, uint64 *partial_write)
{
  float sysRead = 0.0, sysWrite = 0.0;
  uint32 skt = 0;
//  std::cout.setf(std::ios::fixed);
//  std::cout.precision(2);

  while(skt < numSockets)
  {
    if(!(skt % 2) && ((skt+1) < numSockets)) //This is even socket, and it has at least one more socket which can be displayed together
    {
      for(uint64 channel = 0; channel < num_imc_channels; ++channel)
      {
        if(iMC_Rd_socket_chan[skt*num_imc_channels+channel] < 0.0 && iMC_Wr_socket_chan[skt*num_imc_channels+channel] < 0.0) //If the channel read neg. value, the channel is not working; skip it.
            continue;
      }
     sysRead += iMC_Rd_socket[skt];
     sysRead += iMC_Rd_socket[skt+1];
     sysWrite += iMC_Wr_socket[skt];
     sysWrite += iMC_Wr_socket[skt+1];
     skt += 2;
    }
    else //Display one socket in this row
    {
      for(uint64 channel = 0; channel < num_imc_channels; ++channel)
      {
        if(iMC_Rd_socket_chan[skt*num_imc_channels+channel] < 0.0 && iMC_Wr_socket_chan[skt*num_imc_channels+channel] < 0.0) //If the channel read neg. value, the channel is not working; skip it.
            continue;
      }
      sysRead += iMC_Rd_socket[skt];
      sysWrite += iMC_Wr_socket[skt];
      skt += 1;
    }
  }
  printf("%f %f\n", sysRead, sysWrite);
}

/*******************************
*      CALCULATE_BANDWIDTH     *
*******************************/

inline void MySleepMs(int delay_ms)
{
  struct timespec sleep_intrval;
  double complete_seconds;
  sleep_intrval.tv_nsec = static_cast<long>(1000000000.0*(::modf(delay_ms/1000.0,&complete_seconds)));
  sleep_intrval.tv_sec = static_cast<time_t>(complete_seconds);
  ::nanosleep(&sleep_intrval, NULL);
}


/***********************************
*         BANDWIDTHSAMPLERTASK     *
***********************************/

class BandwidthSamplerTask : public PeriodicTask 
{     
  private:
  public:
    bool is_active();
    BandwidthSamplerTask(int interval_time) : PeriodicTask(interval_time) {}
    void task();
    void calculate_bandwidth(PCM *m, const ServerUncorePowerState uncState1[], const ServerUncorePowerState uncState2[], uint64 elapsedTime);

    // Used in main task
    int calibrated;
    long diff_usec;
    double delay;
    ServerUncorePowerState *BeforeState, *AfterState;
    uint64 BeforeTime, AfterTime;
    PCM *m;

    // Used in calculate_bandwidth
    static const uint32 max_sockets = 256;
    static const uint32 max_imc_channels = 8;
    float iMC_Rd_socket_chan[max_sockets][max_imc_channels];
    float iMC_Wr_socket_chan[max_sockets][max_imc_channels];
    float iMC_Rd_socket[max_sockets];
    float iMC_Wr_socket[max_sockets];
    uint64 partial_write[max_sockets];
};

BandwidthSamplerTask* BandwidthSampler::_task    = NULL;
PCM * PCM::instance = NULL;

void BandwidthSamplerTask::calculate_bandwidth(PCM *m, const ServerUncorePowerState uncState1[], const ServerUncorePowerState uncState2[], uint64 elapsedTime)
{
  float sysRead = 0.0;
  uint32 skt = 0;

  for(uint32 skt = 0; skt < m->getNumSockets(); ++skt) {
    iMC_Rd_socket[skt] = 0.0;
    iMC_Wr_socket[skt] = 0.0;
    partial_write[skt] = 0;
    
    for(uint32 channel = 0; channel < max_imc_channels; ++channel) {
      if(getMCCounter(channel,0,uncState1[skt],uncState2[skt]) == 0.0 &&
         getMCCounter(channel,1,uncState1[skt],uncState2[skt]) == 0.0) {
        iMC_Rd_socket_chan[skt][channel] = -1.0;
        iMC_Wr_socket_chan[skt][channel] = -1.0;
        continue;
      }
      iMC_Rd_socket_chan[skt][channel] = (float) (getMCCounter(channel,0,uncState1[skt],uncState2[skt]) *
                                                  64 / 1000000.0 / (elapsedTime/1000.0));
      iMC_Wr_socket_chan[skt][channel] = (float) (getMCCounter(channel,1,uncState1[skt],uncState2[skt]) *
                                                  64 / 1000000.0 / (elapsedTime/1000.0));

      iMC_Rd_socket[skt] += iMC_Rd_socket_chan[skt][channel];
      iMC_Wr_socket[skt] += iMC_Wr_socket_chan[skt][channel];

      partial_write[skt] += (uint64) (getMCCounter(channel,PARTIAL,uncState1[skt],uncState2[skt]) /
                                                  (elapsedTime/1000.0));
      
      }
      display_bandwidth(iMC_Rd_socket_chan[0], iMC_Wr_socket_chan[0], iMC_Rd_socket, iMC_Wr_socket, m->getNumSockets(), max_imc_channels, partial_write);
  }
  printf("There are %u channels.\n", max_imc_channels);
}

void BandwidthSamplerTask::task() 
{
  AfterTime = m->getTickCount();
  for(uint32 i=0; i<m->getNumSockets(); ++i) {
    AfterState[i] = m->getServerUncorePowerState(i);
  }
  
  printf("Calculating bandwidth.\n");
  fflush(stdout);
  calculate_bandwidth(m, BeforeState, AfterState, AfterTime - BeforeTime);

  swap(BeforeTime, AfterTime);
  swap(BeforeState, AfterState);
}

bool BandwidthSampler::is_active() {
  return _task != NULL;
}

void BandwidthSampler::engage() {
  if (!is_active()) {
    _task = new BandwidthSamplerTask(BandwidthSampleInterval);
    
    printf("Initializing variables.\n");
    // Initialize variables
    _task->m = PCM::getInstance();
    _task->calibrated = PCM_CALIBRATION_INTERVAL - 2;
    _task->diff_usec = 0;
    _task->delay = 1.0;
    _task->BeforeState = new ServerUncorePowerState[_task->m->getNumSockets()];
    _task->AfterState = new ServerUncorePowerState[_task->m->getNumSockets()];
    _task->AfterTime = 0;
    _task->BeforeTime = _task->m->getTickCount();
    for(uint32 i = 0; i < _task->m->getNumSockets(); ++i) {
      _task->BeforeState[i] = _task->m->getServerUncorePowerState(i);
    }

    // Now start the task
    _task->enroll();
  }
}

void BandwidthSampler::disengage() {
  if (!is_active()) return;

  // remove BandwidthSamplerTask
  assert(_task != NULL, "sanity check");
  _task->disenroll();
  delete _task;
  _task = NULL;
}

#endif

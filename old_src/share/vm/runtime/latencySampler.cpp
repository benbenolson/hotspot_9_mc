#if 0
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
#include "runtime/latencySampler.hpp"
#include <dirent.h>
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

/* filter out ".", "..", "microcode" in /dev/cpu */
int dir_filter(const struct dirent *dirp)
{
	if (isdigit(dirp->d_name[0]))
		return 1;
	else
		return 0;
}

/*******************************************
 *                  WRMSR                  *
 ******************************************/

void LatencySampler::wrmsr_on_cpu(uint32_t reg, uint64_t regvals, int cpu)
{
	int fd;
	char msr_file_name[64];

	sprintf(msr_file_name, "/dev/cpu/%d/msr", cpu);
	fd = open(msr_file_name, O_WRONLY);
	if (fd < 0) {
		if (errno == ENXIO) {
			fprintf(stderr, "wrmsr: No CPU %d\n", cpu);
			exit(2);
		} else if (errno == EIO) {
			fprintf(stderr, "wrmsr: CPU %d doesn't support MSRs\n",
				cpu);
			exit(3);
		} else {
			perror("wrmsr: open");
			exit(127);
		}
	}

  if (pwrite(fd, &regvals, sizeof(regvals), reg) != sizeof(regvals)) {
    if (errno == EIO) {
      fprintf(stderr,
        "wrmsr: CPU %d cannot set MSR "
        "0x%08"PRIx32" to 0x%016"PRIx64"\n",
        cpu, reg, regvals);
      exit(4);
    } else {
      perror("wrmsr: pwrite");
      exit(127);
    }
  }

	close(fd);

	return;
}

void LatencySampler::wrmsr_on_all_cpus(uint32_t reg, uint64_t regvals)
{
	struct dirent **namelist;
	int dir_entries;

	dir_entries = scandir("/dev/cpu", &namelist, dir_filter, 0);
	while (dir_entries--) {
		wrmsr_on_cpu(reg, atoi(namelist[dir_entries]->d_name), regvals);
		free(namelist[dir_entries]);
	}
	free(namelist);
}

/*************************************************
 *                  RDMSR                        *
 ************************************************/

uint64_t LatencySamplerTask::rdmsr_on_cpu(uint32_t reg, int cpu)
{
	uint64_t data;
	int fd;
	char msr_file_name[64];
	unsigned int bits;

	sprintf(msr_file_name, "/dev/cpu/%d/msr", cpu);
	fd = open(msr_file_name, O_RDONLY);
	if (fd < 0) {
		if (errno == ENXIO) {
			fprintf(stderr, "rdmsr: No CPU %d\n", cpu);
			exit(2);
		} else if (errno == EIO) {
			fprintf(stderr, "rdmsr: CPU %d doesn't support MSRs\n",
				cpu);
			exit(3);
		} else {
			perror("rdmsr: open");
			exit(127);
		}
	}

	if (pread(fd, &data, sizeof(data), reg) != sizeof(data)) {
		if (errno == EIO) {
			fprintf(stderr, "rdmsr: CPU %d cannot read "
				"MSR 0x%08"PRIx32"\n",
				cpu, reg);
			exit(4);
		} else {
			fprintf(stderr, "rdmsr: pread");
      exit(4);
		}
	}

	close(fd);

	bits = highbit - lowbit + 1;
	if (bits < 64) {
		/* Show only part of register */
		data >>= lowbit;
		data &= (1ULL << bits) - 1;
	}

	return data;
}

struct regvals *LatencySamplerTask::rdmsr_on_all_cpus(uint32_t reg)
{
	struct dirent **namelist;
	int dir_entries, i;
  struct regvals *rv = (struct regvals *) malloc(sizeof(struct regvals));

	dir_entries = scandir("/dev/cpu", &namelist, dir_filter, 0);
  rv->vals = (uint64_t *)  malloc(sizeof(uint64_t) * dir_entries);
  rv->numcpu = dir_entries;

	for(i = 0; i < dir_entries; ++i) {
		rdmsr_on_cpu(reg, atoi(namelist[i]->d_name));
		free(namelist[i]);
	}
	free(namelist);

  return rv;
}

void LatencySampler::wrmsr(uint32_t reg, uint64_t regval, int cpu)
{
	if (cpu == -1) {
		wrmsr_on_all_cpus(reg, regval);
	} else {
		wrmsr_on_cpu(reg, regval, cpu);
	}
}

struct regvals *LatencySamplerTask::rdmsr(uint32_t reg, int cpu)
{
  struct regvals *rv;

  if(cpu == -1) {
    return rdmsr_on_all_cpus(reg);
  } else {
    rv = (struct regvals *) malloc(sizeof(struct regvals));
    rv->numcpu = 1;
    rv->vals = (uint64_t *) malloc(sizeof(uint64_t));
    *(rv->vals) = rdmsr_on_cpu(reg, cpu);
    return rv;
  }
}

LatencySamplerTask *LatencySampler::_task = NULL;

void LatencySamplerTask::task() 
{
  curoc = *(rdmsr(C0_CTR0, cpu)->vals);
  curin = *(rdmsr(C0_CTR1, cpu)->vals);
  oldlat = oldoc / oldin;
  curlat = curoc / curin;
  printf("%llu\n", (curoc / curin) - (oldoc / oldin));
  oldoc = curoc;
  oldin = curin;
}

bool LatencySampler::is_active() {
  return _task != NULL;
}

void LatencySampler::engage() {
  if (!is_active()) {
    _task = new LatencySamplerTask(LatencySampleInterval);
    
    /* Start the counters counting */

    /* First let's freeze the counters */
    wrmsr(C0_CTL, BOX_RESET_AND_FREEZE, cpu);

    /* Now set to get the events */
    wrmsr(C0_CTL0, TOR_OCCUPANCY_MISS_OPCODE, cpu);
    wrmsr(C0_CTL1, TOR_INSERTS_MISS_OPCODE, cpu);

    /* Set the filter */
    wrmsr(C0_FILTER1, DRD_OPCODE_FILTER, cpu);

    /* Unfreeze the counters */
    wrmsr(C0_CTL, BOX_UNFREEZE, cpu);

    gettimeofday(&start_tv, NULL);
    start_millis = start_tv.tv_sec * 1000 + start_tv.tv_usec / 1000;

    /* Now read the values */
    oldoc = *(rdmsr(C0_CTR0, cpu)->vals);
    oldin = *(rdmsr(C0_CTR1, cpu)->vals);
    
    /* Finally, start the task to read the counters every so often */
    _task->enroll();
  }
}

void LatencySampler::disengage() {
  if (!is_active()) return;

  // remove LatencySamplerTask
  assert(_task != NULL, "sanity check");
  _task->disenroll();
  delete _task;
  _task = NULL;
}

#endif
#endif

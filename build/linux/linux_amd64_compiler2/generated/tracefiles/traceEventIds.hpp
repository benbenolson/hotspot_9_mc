/* AUTOMATICALLY GENERATED FILE - DO NOT EDIT */

#ifndef TRACEFILES_TRACEEVENTIDS_HPP
#define TRACEFILES_TRACEEVENTIDS_HPP

#include "utilities/macros.hpp"
#if INCLUDE_TRACE
#include "trace/traceDataTypes.hpp"

/**
 * Enum of the event types in the JVM
 */
enum TraceEventId {
  _traceeventbase = (NUM_RESERVED_EVENTS-1), // Make sure we start at right index.
  
  // Events -> enum entry
  TraceThreadStartEvent,
  TraceThreadEndEvent,
  TraceThreadSleepEvent,
  TraceThreadParkEvent,
  TraceJavaMonitorEnterEvent,
  TraceJavaMonitorWaitEvent,
  TraceClassLoadEvent,
  TraceClassUnloadEvent,
  TraceIntFlagChangedEvent,
  TraceUnsignedIntFlagChangedEvent,
  TraceLongFlagChangedEvent,
  TraceUnsignedLongFlagChangedEvent,
  TraceDoubleFlagChangedEvent,
  TraceBooleanFlagChangedEvent,
  TraceStringFlagChangedEvent,
  TraceGCHeapSummaryEvent,
  TraceMetaspaceSummaryEvent,
  TraceMetaspaceGCThresholdEvent,
  TraceMetaspaceAllocationFailureEvent,
  TraceMetaspaceOOMEvent,
  TraceMetaspaceChunkFreeListSummaryEvent,
  TracePSHeapSummaryEvent,
  TraceG1HeapSummaryEvent,
  TraceGCGarbageCollectionEvent,
  TraceGCParallelOldEvent,
  TraceGCYoungGarbageCollectionEvent,
  TraceGCOldGarbageCollectionEvent,
  TraceGCG1GarbageCollectionEvent,
  TraceGCG1MMUEvent,
  TraceEvacuationInfoEvent,
  TraceGCReferenceStatisticsEvent,
  TraceObjectCountAfterGCEvent,
  TracePromoteObjectInNewPLABEvent,
  TracePromoteObjectOutsidePLABEvent,
  TracePromotionFailedEvent,
  TraceEvacuationFailedEvent,
  TraceConcurrentModeFailureEvent,
  TraceGCPhasePauseEvent,
  TraceGCPhasePauseLevel1Event,
  TraceGCPhasePauseLevel2Event,
  TraceGCPhasePauseLevel3Event,
  TraceCompilationEvent,
  TraceCompilerPhaseEvent,
  TraceCompilerFailureEvent,
  TraceCompilerInliningEvent,
  TraceSweepCodeCacheEvent,
  TraceCodeCacheFullEvent,
  TraceExecuteVMOperationEvent,
  TraceAllocObjectInNewTLABEvent,
  TraceAllocObjectOutsideTLABEvent,

  MaxTraceEventId
};

/**
 * Struct types in the JVM
 */
enum TraceStructId {
  TraceThreadStruct,
  TraceVMThreadStruct,
  TraceJavaThreadStruct,
  TraceThreadGroupStruct,
  TraceClassStruct,
  TraceMethodStruct,
  TraceUTFConstantStruct,
  TraceThreadStateStruct,
  TraceGCNameStruct,
  TraceGCCauseStruct,
  TraceGCWhenStruct,
  TraceG1YCTypeStruct,
  TraceGCThresholdUpdaterStruct,
  TraceReferenceTypeStruct,
  TraceMetadataTypeStruct,
  TraceMetaspaceObjectTypeStruct,
  TraceNARROW_OOP_MODEStruct,
  TraceVMOperationTypeStruct,
  TraceCompilerPhaseTypeStruct,
  TraceFlagValueOriginStruct,
  TraceCodeBlobTypeStruct,
  TraceThreadStartStruct,
  TraceThreadEndStruct,
  TraceThreadSleepStruct,
  TraceThreadParkStruct,
  TraceJavaMonitorEnterStruct,
  TraceJavaMonitorWaitStruct,
  TraceClassLoadStruct,
  TraceClassUnloadStruct,
  TraceIntFlagChangedStruct,
  TraceUnsignedIntFlagChangedStruct,
  TraceLongFlagChangedStruct,
  TraceUnsignedLongFlagChangedStruct,
  TraceDoubleFlagChangedStruct,
  TraceBooleanFlagChangedStruct,
  TraceStringFlagChangedStruct,
  TraceVirtualSpaceStruct,
  TraceObjectSpaceStruct,
  TraceGCHeapSummaryStruct,
  TraceMetaspaceSizesStruct,
  TraceMetaspaceSummaryStruct,
  TraceMetaspaceGCThresholdStruct,
  TraceMetaspaceAllocationFailureStruct,
  TraceMetaspaceOOMStruct,
  TraceMetaspaceChunkFreeListSummaryStruct,
  TracePSHeapSummaryStruct,
  TraceG1HeapSummaryStruct,
  TraceGCGarbageCollectionStruct,
  TraceGCParallelOldStruct,
  TraceGCYoungGarbageCollectionStruct,
  TraceGCOldGarbageCollectionStruct,
  TraceGCG1GarbageCollectionStruct,
  TraceGCG1MMUStruct,
  TraceEvacuationInfoStruct,
  TraceGCReferenceStatisticsStruct,
  TraceCopyFailedStruct,
  TraceObjectCountAfterGCStruct,
  TracePromoteObjectInNewPLABStruct,
  TracePromoteObjectOutsidePLABStruct,
  TracePromotionFailedStruct,
  TraceEvacuationFailedStruct,
  TraceConcurrentModeFailureStruct,
  TraceGCPhasePauseStruct,
  TraceGCPhasePauseLevel1Struct,
  TraceGCPhasePauseLevel2Struct,
  TraceGCPhasePauseLevel3Struct,
  TraceCompilationStruct,
  TraceCompilerPhaseStruct,
  TraceCompilerFailureStruct,
  TraceCiMethodStruct,
  TraceCompilerInliningStruct,
  TraceSweepCodeCacheStruct,
  TraceCodeCacheFullStruct,
  TraceExecuteVMOperationStruct,
  TraceAllocObjectInNewTLABStruct,
  TraceAllocObjectOutsideTLABStruct,

  MaxTraceStructId
};

typedef enum TraceEventId  TraceEventId;
typedef enum TraceStructId TraceStructId;

#endif // INCLUDE_TRACE
#endif // TRACEFILES_TRACEEVENTIDS_HPP

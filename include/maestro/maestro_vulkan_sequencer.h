#ifndef MAESTRO_VULKAN_SEQUENCER_H
#define MAESTRO_VULKAN_SEQUENCER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <maestro/maestro_vulkan.h>


/* ================================================================================ */
/*  TYPEDEF                                                                         */
/* ================================================================================ */

typedef struct MaestroVulkanSequencerHandler MaestroVulkanSequencerHandler;
typedef struct MaestroVulkanRecorder MaestroVulkanRecorder;

typedef u8 MaestroVulkanQueueKind;
enum {
    MAESTRO_VULKAN_QUEUE_GRAPHICS,
    MAESTRO_VULKAN_QUEUE_COMPUTE,
    MAESTRO_VULKAN_QUEUE_TRANSFER,
};

typedef u8 MaestroVulkanCueState;
enum {
    MAESTRO_VULKAN_CUE_OPEN,       /* allocated, not yet submitted */
    MAESTRO_VULKAN_CUE_RECORDED,   /* submitted, not yet flushed to the GPU */
    MAESTRO_VULKAN_CUE_IN_FLIGHT,  /* handed to the GPU */
    MAESTRO_VULKAN_CUE_DONE,       /* GPU work completed */
    MAESTRO_VULKAN_CUE_FAILED,     /* queue submission failed; the work never ran */
    MAESTRO_VULKAN_CUE_RETIRED,    /* stale handle: the cue completed and was recycled */
};

#define MAESTRO_VULKAN_MAX_CUES 256 /* default cue pool capacity, see creator */
#define MAESTRO_VULKAN_CUE_MAX_WAITS 8
#define MAESTRO_VULKAN_CUE_MAX_BINARY 4
#define MAESTRO_VULKAN_RECORDER_MAX_BUFS 8


/* ================================================================================ */
/*  SEQUENCER HANDLER                                                               */
/* ================================================================================ */

#define MAESTRO_VULKAN_SEQUENCER_HANDLER_NAME "MaestroVulkanSequencerHandler"
#define MAESTRO_VULKAN_SEQUENCER_HANDLER_VERSION HARP_MAKE_VERSION(1,0,0)

typedef struct MaestroVulkanSequencerCreator {
    HarpCreatorBase _base;
    u32 cue_capacity; /* 0 selects MAESTRO_VULKAN_MAX_CUES */
} MaestroVulkanSequencerCreator;

/* A cue is a value type identifying one submission. A held cue is stable
   until cue_release: it is never recycled out from under its owner. Once
   released (or once observed complete and recycled) the handle is stale,
   and a stale handle reads as finished work: cue_done returns 1, cue_wait
   returns HARP_RESULT_OK, cue_state returns RETIRED. A zero-initialized
   cue is the null cue and behaves like any stale handle, so a default
   value can always be waited on safely. */
typedef struct MaestroVulkanCue {
    u32 index;
    u32 gen;
} MaestroVulkanCue;

typedef struct MaestroVulkanSubmitDesc {
    VkCommandBuffer cmd;

    /* Waits must reference cues of the same VkDevice; a cross-device
       dependency is rejected at submit. Oversized wait or signal lists are
       rejected rather than truncated. */
    const MaestroVulkanCue *waits;
    u32 wait_count;

    const VkSemaphore *binary_waits;
    const VkPipelineStageFlags2 *binary_wait_stages;
    u32 binary_wait_count;
    const VkSemaphore *binary_signals;
    u32 binary_signal_count;

    VkFence extra_fence;
} MaestroVulkanSubmitDesc;

/* Threading contract:
   - A recorder is single-owner: one thread (or one frame slot) at a time.
     Calls on the same recorder must not overlap.
   - submit is safe from any thread; it enqueues onto the recorder's queue
     slot and defers the driver call. Work reaches the GPU on flush/conduct,
     or when cue_wait meets an unflushed cue (it flushes on the caller's
     behalf). Prefer conduct from one thread per frame: flush pushes a single
     queue and can leave that work waiting on dependencies whose own queue
     has not been flushed yet.
   - Cues may be passed to and waited on from any thread.
   - cue_wait returns HARP_RESULT_OK on completion (or a stale handle),
     HARP_RESULT_INVALID_STATE on timeout (not done yet, retry), and
     HARP_RESULT_FAILED if the submission failed or the wait errored. */
struct MaestroVulkanSequencerHandler {
    HarpHandlerBase _base;

    HarpResult (*open_recorder)(MaestroVulkanSequencerHandler *h, MaestroVulkanDeviceActor *device, MaestroVulkanQueueKind kind, MaestroVulkanRecorder **out);
    HarpResult (*close_recorder)(MaestroVulkanRecorder *rec);
    VkCommandBuffer (*record)(MaestroVulkanRecorder *rec);
    MaestroVulkanCue (*submit)(MaestroVulkanRecorder *rec, const MaestroVulkanSubmitDesc *desc);
    HarpResult (*reset_recorder)(MaestroVulkanRecorder *rec);

    HarpResult (*flush)(MaestroVulkanRecorder *rec);
    HarpResult (*conduct)(MaestroVulkanSequencerHandler *h);

    MaestroVulkanCueState (*cue_state)(MaestroVulkanSequencerHandler *h, MaestroVulkanCue cue);
    b8 (*cue_done)(MaestroVulkanSequencerHandler *h, MaestroVulkanCue cue);
    HarpResult (*cue_wait)(MaestroVulkanSequencerHandler *h, MaestroVulkanCue cue, u64 timeout_ns);
    void (*cue_release)(MaestroVulkanSequencerHandler *h, MaestroVulkanCue cue);
};


#ifdef __cplusplus
}
#endif

#endif /* MAESTRO_VULKAN_SEQUENCER_H */

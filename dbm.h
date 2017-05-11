/*
  This file is part of MAMBO, a low-overhead dynamic binary modification tool:
      https://github.com/beehive-lab/mambo

  Copyright 2013-2017 Cosmin Gorgovan <cosmin at linux-geek dot org>

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

#ifndef __DBM_H__
#define __DBM_H__

#include <stdbool.h>
#include <signal.h>
#include <limits.h>
#include <stdint.h>

#ifdef __arm__
#include "pie/pie-arm-decoder.h"
#include "pie/pie-thumb-decoder.h"
#endif

#include "common.h"
#include "util.h"

/* Various parameters which can be tuned */
#define TOTAL_CC_SIZE (16*1024*1024)
#ifdef DBM_TRACES
  #define BB_META_SIZE 55000
  #define TRACE_CACHE_SIZE (2*1024*1024)
  #define TRACE_META_SIZE 60000
#else
  #define BB_META_SIZE 65000
  #define TRACE_CACHE_SIZE 0
  #define TRACE_META_SIZE 0
#endif
#define BB_CACHE_SIZE (TOTAL_CC_SIZE - TRACE_CACHE_SIZE)

#define MIN_FSPACE_UNIT 1024
#ifdef PLUGINS_NEW
  #define MIN_FSPACE (MIN_FSPACE_UNIT * (1 + global_data.free_plugin))
#else
  #define MIN_FSPACE (MIN_FSPACE_UNIT)
#endif

#if (TRACE_CACHE_SIZE >= TOTAL_CC_SIZE)
  #error The trace cache size must be smaller than the total code cache size
  // Redefine this to prevent array size errors from showing up later
  #undef  TRACE_CACHE_SIZE
  #define TRACE_CACHE_SIZE TOTAL_CC_SIZE
#endif

#define TRACE_ALIGN 4 // must be a power of 2
#define TRACE_ALIGN_MASK (TRACE_ALIGN-1)

#define INST_CNT 400

#define MAX_TB_INDEX  152
#define TB_CACHE_SIZE 32

#define MAX_BACK_INLINE 5
#define MAX_TRACE_FRAGMENTS 20

#define RAS_SIZE (4096*5)
#define TBB_TARGET_REACHED_SIZE 30

#define MAX_CC_LINKS 100000

#define THUMB 0x1
#define FULLADDR 0x2

#define MAX_PLUGIN_NO (10)

typedef enum {
  mambo_bb = 0,
  mambo_trace,
  mambo_trace_entry
} cc_type;

typedef enum {
  unknown,
  stub,
  trace_inline_max,
#ifdef __arm__
  uncond_b_to_bl_thumb,
  uncond_imm_thumb,
  uncond_reg_thumb,
  cond_imm_thumb,
  cond_reg_thumb,
  cbz_thumb,
  uncond_blxi_thumb,
  cond_blxi_thumb,
  cond_imm_arm,
  uncond_imm_arm,
  cond_reg_arm,
  uncond_reg_arm,
  uncond_blxi_arm,
  cond_blxi_arm,
  tbb,
  tbh,
  tb_indirect,
  pred_bxlr,
  pred_pop16pc,
  pred_ldmfd32pc,
  pred_armbxlr,
  pred_ldrpcsp,
  pred_armldmpc,
  pred_bad,
#endif //__arm__
#ifdef __aarch64__
  uncond_imm_a64,
  uncond_branch_reg,
  cond_imm_a64,
  cbz_a64,
  tbz_a64,
#endif // __aarch64__
} branch_type;

typedef struct {
  uint8_t bbs[BB_CACHE_SIZE];
  uint8_t traces[TRACE_CACHE_SIZE];
} dbm_code_cache;

#define FALLTHROUGH_LINKED (1 << 0)
#define BRANCH_LINKED (1 << 1)
#define BOTH_LINKED (1 << 2)

typedef struct {
  uint16_t *source_addr;
  uintptr_t tpc;
  branch_type exit_branch_type;
#ifdef __arm__
  uint16_t *exit_branch_addr;
#endif // __arm__
#ifdef __aarch64__
  uint32_t *exit_branch_addr;
#endif // __arch64__
  uintptr_t branch_taken_addr;
  uintptr_t branch_skipped_addr;
  uintptr_t branch_condition;
  uintptr_t branch_cache_status;
  uint32_t rn;
  uint32_t free_b;
  ll_entry *linked_from;
} dbm_code_cache_meta;

typedef struct {
  unsigned long flags;
  void *child_stack;
  pid_t *ptid;
  uintptr_t tls;
  pid_t *ctid;
} sys_clone_args;

struct trace_exits {
  uintptr_t from;
  uintptr_t to;
};

#define MAX_TRACE_REC_EXITS (MAX_TRACE_FRAGMENTS+1)
typedef struct {
  int id;
  int source_bb;
  void *write_p;
  uintptr_t entry_addr;
  bool active;
  int free_exit_rec;
  struct trace_exits exits[MAX_TRACE_REC_EXITS];
} trace_in_prog;

enum dbm_thread_status {
  THREAD_RUNNING = 0,
  THREAD_SYSCALL,
  THREAD_EXIT
};

typedef struct dbm_thread_s dbm_thread;
struct dbm_thread_s {
  dbm_thread *next_thread;
  enum dbm_thread_status status;

  int free_block;
  bool was_flushed;
  uintptr_t dispatcher_addr;
  uintptr_t syscall_wrapper_addr;

  dbm_code_cache *code_cache;
  dbm_code_cache_meta code_cache_meta[BB_META_SIZE + TRACE_META_SIZE];
  hash_table entry_address;
  void *bb_cache_next;
#ifdef DBM_TRACES
  hash_table trace_entry_address;

  uint8_t   exec_count[BB_META_SIZE];
  uintptr_t trace_head_incr_addr;
  uint8_t  *trace_cache_next;
  int       trace_id;
  int       trace_fragment_count;
  trace_in_prog active_trace;
#endif

  ll *cc_links;

  uintptr_t tls;
  uintptr_t child_tls;

#ifdef PLUGINS_NEW
  void *plugin_priv[MAX_PLUGIN_NO];
#endif
  void *clone_ret_addr;
  volatile pid_t tid;
  sys_clone_args *clone_args;
  bool clone_vm;
  int pending_signals[_NSIG];
  uint32_t is_signal_pending;
};

typedef enum {
  ARM_INST,
  THUMB_INST,
  A64_INST,
} inst_set;

#include "api/plugin_support.h"

typedef struct {
  int argc;
  char **argv;
  interval_map exec_allocs;
  uintptr_t signal_handlers[_NSIG];
  pthread_mutex_t signal_handlers_mutex;
  uintptr_t brk;
  uintptr_t initial_brk;
  pthread_mutex_t brk_mutex;

  dbm_thread *threads;
  pthread_mutex_t thread_list_mutex;

  volatile int exit_group;
#ifdef PLUGINS_NEW
  int free_plugin;
  mambo_plugin plugins[MAX_PLUGIN_NO];
#endif
} dbm_global;

typedef struct {
  uintptr_t tpc;
  uintptr_t spc;
} cc_addr_pair;

#define MAX_SCAN_QUEUE_LEN (10)
#define QUEUE_COND_MASK (0xF)
#define QUEUE_STUB_ONLY (1 << 4)
#define QUEUE_IS_RAW_ADDR (1 << 5)
#define QUEUE_IS_THUMB (1 << 6)
typedef struct {
  uintptr_t spc;
  void *link_to;
  uint32_t info;
} scanner_queue_entry;

typedef struct {
  int len;
  scanner_queue_entry entries[MAX_SCAN_QUEUE_LEN];
} scanner_queue_t;

void dbm_exit(dbm_thread *thread_data, uint32_t code);
void thread_abort(dbm_thread *thread_data);

extern void dispatcher_trampoline();
extern void syscall_wrapper();
extern void trace_head_incr();
extern void* start_of_dispatcher_s;
extern void* end_of_dispatcher_s;
extern void th_to_arm();
extern void th_enter(void *stack, uintptr_t cc_addr);
extern void send_self_signal();
extern void syscall_wrapper_svc();

int lock_thread_list(void);
int unlock_thread_list(void);
int register_thread(dbm_thread *thread_data, bool caller_has_lock);
int unregister_thread(dbm_thread *thread_data, bool caller_has_lock);
bool allocate_thread_data(dbm_thread **thread_data);
int free_thread_data(dbm_thread *thread_data);
void init_thread(dbm_thread *thread_data);
void reset_process(dbm_thread *thread_data);

uintptr_t cc_lookup(dbm_thread *thread_data, uintptr_t target);
uintptr_t lookup_or_scan(dbm_thread *thread_data, uintptr_t target, bool *cached);
uintptr_t lookup_or_stub(dbm_thread *thread_data, uintptr_t target);
uintptr_t scan(dbm_thread *thread_data, uint16_t *address, int stub_id);
size_t scan_arm(dbm_thread *thread_data, uint32_t *read_address, int basic_block, cc_type type,
                  uint32_t *write_p, scanner_queue_t *scan_queue);
size_t scan_thumb(dbm_thread *thread_data, uint16_t *read_address, int basic_block, cc_type type,
                    uint16_t *write_p, scanner_queue_t *scan_queue);
size_t scan_a64(dbm_thread *thread_data, uint32_t *read_address, int basic_block, cc_type type,
                  uint32_t *write_p, scanner_queue_t *scan_queue);
int allocate_bb(dbm_thread *thread_data);
void trace_dispatcher(uintptr_t target, uintptr_t *next_addr, uint32_t source_index, dbm_thread *thread_data);
void flush_code_cache(dbm_thread *thread_data);
#ifdef __aarch64__
void generate_trace_exit(dbm_thread *thread_data, uint32_t **o_write_p, int fragment_id, bool is_taken);
#endif
void insert_cond_exit_branch(dbm_code_cache_meta *bb_meta, void **o_write_p, int cond);
void sigret_dispatcher_call(dbm_thread *thread_data, ucontext_t *cont, uintptr_t target);

void thumb_encode_stub_bb(dbm_thread *thread_data, uint16_t **o_write_p, int basic_block, uint32_t target);
void arm_encode_stub_bb(dbm_thread *thread_data, uint32_t **o_write_p, int basic_block, uint32_t target);
#ifdef __arm__
int scanner_queue_add(scanner_queue_t *queue, void *link_to, uintptr_t spc, mambo_cond cond, bool is_thumb,
                      bool is_raw_addr, bool stub_only);
#else
int scanner_queue_add(scanner_queue_t *queue, void *link_to, uintptr_t spc, bool is_raw_addr, bool stub_only);
#endif
void scanner_queue_process(dbm_thread *thread_data, scanner_queue_t *queue);
void rewrite_cc_branches(dbm_thread *thread_data, int bb_id, uintptr_t new_target);

int addr_to_bb_id(dbm_thread *thread_data, uintptr_t addr);
int addr_to_fragment_id(dbm_thread *thread_data, uintptr_t addr);
void record_cc_link(dbm_thread *thread_data, uintptr_t linked_from, uintptr_t linked_to_addr);
bool is_bb(dbm_thread *thread_data, uintptr_t addr);
void install_system_sig_handlers();

inline static uintptr_t adjust_cc_entry(uintptr_t addr) {
#ifdef __arm__
  if (addr != UINT_MAX) {
    addr += 4 - ((addr & 1) << 1); // +4 for ARM, +2 for Thumb
  }
#endif
  return addr;
}

extern dbm_global global_data;
extern dbm_thread *disp_thread_data;
extern uint32_t *th_is_pending_ptr;
extern __thread dbm_thread *current_thread;

#ifdef PLUGINS_NEW
void set_mambo_context(mambo_context *ctx, dbm_thread *thread_data, inst_set inst_type,
                       cc_type fragment_type, int fragment_id, int inst, mambo_cond cond,
                       void *read_address, void *write_p, unsigned long *args);
void mambo_deliver_callbacks(unsigned cb_id, dbm_thread *thread_data, inst_set inst_type,
                             cc_type fragment_type, int fragment_id, int inst, mambo_cond cond,
                             void *read_address, void *write_p, unsigned long *regs);
#endif

#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))

/* Constants */

#define ALLOCATE_BB 0

#ifdef CC_HUGETLB
  #define CC_PAGE_SIZE (2*1024*1024)
  #define CC_MMAP_OPTS (MAP_PRIVATE|MAP_ANONYMOUS|MAP_HUGETLB)
#else
  #define CC_PAGE_SIZE 4096
  #define CC_MMAP_OPTS (MAP_PRIVATE|MAP_ANONYMOUS)
#endif

#ifdef METADATA_HUGETLB
  #define METADATA_PAGE_SIZE (2*1024*1024)
  #define METADATA_MMAP_OPTS (MAP_PRIVATE|MAP_ANONYMOUS|MAP_HUGETLB)
#else
  #define METADATA_PAGE_SIZE 4096
  #define METADATA_MMAP_OPTS (MAP_PRIVATE|MAP_ANONYMOUS)
#endif

#define ROUND_UP(input, multiple_of) \
  ((((input) / (multiple_of)) * (multiple_of)) + (((input) % (multiple_of)) ? (multiple_of) : 0))

#define CC_SZ_ROUND(input) ROUND_UP(input, CC_PAGE_SIZE)
#define METADATA_SZ_ROUND(input) ROUND_UP(input, CC_PAGE_SIZE)

#define PAGE_SIZE 4096

#define trampolines_size_bytes         ((uintptr_t)&end_of_dispatcher_s - (uintptr_t)&start_of_dispatcher_s)
#define trampolines_size_bbs           ((trampolines_size_bytes / sizeof(dbm_block)) \
                                      + ((trampolines_size_bytes % sizeof(dbm_block)) ? 1 : 0))

#define UNLINK_SIGNAL (SIGILL)
#define CPSR_T (0x20)

#ifdef __arm__
  #define context_pc uc_mcontext.arm_pc
  #define context_sp uc_mcontext.arm_sp
  #define context_reg(reg) uc_mcontext.arm_r##reg
#elif __aarch64__
  #define context_pc uc_mcontext.pc
  #define context_sp uc_mcontext.sp
  #define context_reg(reg) uc_mcontext.regs[reg]
#endif

#endif


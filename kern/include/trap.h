/* See COPYRIGHT for copyright information. */

#ifndef ROS_KERN_TRAP_H
#define ROS_KERN_TRAP_H
#ifndef ROS_KERNEL
# error "This is an ROS kernel header; user programs should not #include it"
#endif

#include <arch/arch.h>
#include <arch/mmu.h>
#include <arch/trap.h>
#include <sys/queue.h>

// func ptr for interrupt service routines
typedef void ( *poly_isr_t)(trapframe_t* tf, TV(t) data);
typedef void (*isr_t)(trapframe_t* tf, void * data);
typedef struct InterruptHandler {
	poly_isr_t isr;
	TV(t) data;
} handler_t;

#ifdef __IVY__
#pragma cilnoremove("iht_lock")
extern spinlock_t iht_lock;
#endif
extern handler_t LCKD(&iht_lock) (CT(NUM_INTERRUPT_HANDLERS) RO interrupt_handlers)[];

void idt_init(void);
void
register_interrupt_handler(handler_t SSOMELOCK (CT(NUM_INTERRUPT_HANDLERS)table)[],
                           uint8_t int_num,
                           poly_isr_t handler, TV(t) data);
void print_trapframe(trapframe_t *tf);
void page_fault_handler(trapframe_t *tf);
/* Generic per-core timer interrupt handler.  set_percore_timer() will fire the
 * timer_interrupt(). */
void set_core_timer(uint32_t usec, bool periodic);
void timer_interrupt(struct trapframe *tf, void *data);

void sysenter_init(void);
extern void sysenter_handler();

void save_fp_state(struct ancillary_state *silly);
void restore_fp_state(struct ancillary_state *silly);
/* Set stacktop for the current core to be the stack the kernel will start on
 * when trapping/interrupting from userspace */
void set_stack_top(uintptr_t stacktop);
uintptr_t get_stack_top(void);

/* It's important that this is inline and that tf is not a stack variable */
static inline void save_kernel_tf(struct trapframe *tf)
                   __attribute__((always_inline));
void pop_kernel_tf(struct trapframe *tf) __attribute__((noreturn));

/* Sends a non-maskable interrupt, which we have print a trapframe. */
void send_nmi(uint32_t os_coreid);

/* Kernel messages.  Each arch implements them in their own way.  Both should be
 * guaranteeing in-order delivery.  Kept here in trap.h, since sparc is using
 * trap.h for KMs.  Eventually, both arches will use the same implementation.
 *
 * These are different (for now) than the smp_calls in smp.h, since
 * they will be executed immediately (for urgent messages), and in the order in
 * which they are sent.  smp_calls are currently not run in order, and they must
 * return (possibly passing the work to a workqueue, which is really just a
 * routine message, so they really need to just return).
 *
 * Eventually, smp_call will be replaced by these.
 *
 * Also, a big difference is that smp_calls can use the same message (registered
 * in the interrupt_handlers[] for x86) for every recipient, but the kernel
 * messages require a unique message.  Also for now, but it might be like that
 * for a while on x86 (til we have a broadcast). */

#define KMSG_IMMEDIATE 			1
#define KMSG_ROUTINE 			2

typedef void (*amr_t)(struct trapframe *tf, uint32_t srcid, long a0, long a1,
                      long a2);

/* Must stay 8-byte aligned for sparc */
struct kernel_message
{
	STAILQ_ENTRY(kernel_message) link;
	uint32_t srcid;
	uint32_t dstid;
	amr_t pc;
	long arg0;
	long arg1;
	long arg2;
}__attribute__((aligned(8)));

STAILQ_HEAD(kernel_msg_list, kernel_message);
typedef struct kernel_message kernel_message_t;

void kernel_msg_init(void);
uint32_t send_kernel_message(uint32_t dst, amr_t pc, long arg0, long arg1,
                             long arg2, int type);
void handle_kmsg_ipi(struct trapframe *tf, void *data);
void process_routine_kmsg(void);
void print_kmsgs(uint32_t coreid);

/* Kernel context depths.  IRQ depth is how many nested IRQ stacks/contexts we
 * are working on.  Kernel trap depth is how many nested kernel traps (not
 * user-space traps) we have.
 *
 * Some examples:
 * 		(original context in parens, +(x, y) is the change to IRQ and ktrap
 * 		depth):
 * - syscall (user): +(0, 0)
 * - trap (user): +(0, 0)
 * - irq (user): +(1, 0)
 * - irq (kernel, handling syscall): +(1, 0)
 * - trap (kernel, regardless of context): +(0, 1)
 * - NMI (kernel): it's actually a kernel trap, even though it is
 *   sent by IPI.  +(0, 1)
 * - NMI (user): just a trap.  +(0, 0)
 *
 * So if the user traps in for a syscall (0, 0), then the kernel takes an IRQ
 * (1, 0), and then another IRQ (2, 0), and then the kernel page faults (a
 * trap), we're at (2, 1).
 *
 * Or if we're in userspace, then an IRQ arrives, we're in the kernel at (1, 0).
 * Note that regardless of whether or not we are in userspace or the kernel when
 * an irq arrives, we still are only at level 1 irq depth.  We don't care if we
 * have one or 0 kernel contexts under us.  (The reason for this is that I care
 * if it is *possible* for us to interrupt the kernel, not whether or not it
 * actually happened). */

/* uint32_t __ctx_depth is laid out like so:
 *
 * +------8------+------8------+------8------+------8------+
 * |    Flags    |    Unused   | Kernel Trap |  IRQ Depth  |
 * |             |             |    Depth    |             |
 * +-------------+-------------+-------------+-------------+
 *
 */
#define __CTX_IRQ_D_SHIFT			0
#define __CTX_KTRAP_D_SHIFT			8
#define __CTX_FLAG_SHIFT			24
#define __CTX_IRQ_D_MASK			((1 << 8) - 1)
#define __CTX_KTRAP_D_MASK			((1 << 8) - 1)
#define __CTX_NESTED_CTX_MASK		((1 << 16) - 1)
#define __CTX_EARLY_RKM 			(1 << __CTX_FLAG_SHIFT)

/* Basic functions to get or change depths */

#define irq_depth(pcpui)                                                       \
	(((pcpui)->__ctx_depth >> __CTX_IRQ_D_SHIFT) & __CTX_IRQ_D_MASK)

#define ktrap_depth(pcpui)                                                     \
	(((pcpui)->__ctx_depth >> __CTX_KTRAP_D_SHIFT) & __CTX_KTRAP_D_MASK)

#define inc_irq_depth(pcpui)                                                   \
	((pcpui)->__ctx_depth += 1 << __CTX_IRQ_D_SHIFT)

#define dec_irq_depth(pcpui)                                                   \
	((pcpui)->__ctx_depth -= 1 << __CTX_IRQ_D_SHIFT)

#define inc_ktrap_depth(pcpui)                                                 \
	((pcpui)->__ctx_depth += 1 << __CTX_KTRAP_D_SHIFT)

#define dec_ktrap_depth(pcpui)                                                 \
	((pcpui)->__ctx_depth -= 1 << __CTX_KTRAP_D_SHIFT)

#define set_rkmsg(pcpui)                                                       \
	((pcpui)->__ctx_depth |= __CTX_EARLY_RKM)

#define clear_rkmsg(pcpui)                                                     \
	((pcpui)->__ctx_depth &= ~__CTX_EARLY_RKM)

/* Functions to query the kernel context depth/state.  I haven't fully decided
 * on whether or not 'default' context includes RKMs or not.  Will depend on
 * how we use it.  Check the code below to see what the latest is. */

#define in_irq_ctx(pcpui)                                                      \
	(irq_depth(pcpui))

#define in_early_rkmsg_ctx(pcpui)                                              \
	((pcpui)->__ctx_depth & __CTX_EARLY_RKM)

/* Right now, anything (KTRAP, IRQ, or RKM) makes us not 'default' */
#define in_default_ctx(pcpui)                                                  \
	(!(pcpui)->__ctx_depth)

/* Can block only if we have no nested contexts (ktraps or irqs, (which are
 * potentially nested contexts)) */
#define can_block(pcpui)                                                       \
	(!((pcpui)->__ctx_depth & __CTX_NESTED_CTX_MASK))

/* TRUE if we are allowed to spin, given that the 'lock' was declared as not
 * grabbable from IRQ context.  Meaning, we can't grab the lock from any nested
 * context.  (And for most locks, we can never grab them from a kernel trap
 * handler). 
 *
 * Example is a lock that is not declared as irqsave, but we later grab it from
 * irq context.  This could deadlock the system, even if it doesn't do it this
 * time.  This function will catch that. */
#define can_spinwait_noirq(pcpui)                                              \
	(!((pcpui)->__ctx_depth & __CTX_NESTED_CTX_MASK))

/* TRUE if we are allowed to spin, given that the 'lock' was declared as
 * potentially grabbable by IRQ context (such as with an irqsave lock).  We can
 * never grab from a ktrap, since there is no way to prevent that.  And we must
 * have IRQs disabled, since an IRQ handler could attempt to grab the lock. */
#define can_spinwait_irq(pcpui)                                                \
	((!ktrap_depth(pcpui) && !irq_is_enabled()))

/* Debugging */
void print_kctx_depths(const char *str);
 
#endif /* ROS_KERN_TRAP_H */

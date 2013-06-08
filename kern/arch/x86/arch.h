#ifndef ROS_INC_ARCH_H
#define ROS_INC_ARCH_H

#include <ros/arch/arch.h>
#include <ros/common.h>
#include <arch/x86.h>
#include <arch/apic.h>

/* Arch Constants */
#define ARCH_CL_SIZE				 64
/* Top of the kernel virtual mapping area (KERNBASE) */
/* For sanity reasons, I don't plan to map the top page */
#define KERN_VMAP_TOP				0xfffff000

static __inline void breakpoint(void) __attribute__((always_inline));
static __inline void invlpg(void *SNT addr) __attribute__((always_inline));
static __inline void tlbflush(void) __attribute__((always_inline));
static __inline void icache_flush_page(void* va, void* kva) __attribute__((always_inline));
static __inline uint64_t read_tsc(void) __attribute__((always_inline));
static __inline uint64_t read_tsc_serialized(void) __attribute__((always_inline));
static __inline void enable_irq(void) __attribute__((always_inline));
static __inline void disable_irq(void) __attribute__((always_inline));
static __inline void enable_irqsave(int8_t* state) __attribute__((always_inline));
static __inline void disable_irqsave(int8_t* state) __attribute__((always_inline));
static __inline void cpu_relax(void) __attribute__((always_inline));
static __inline void cpu_halt(void) __attribute__((always_inline));
static __inline void clflush(uintptr_t* addr) __attribute__((always_inline));
static __inline int irq_is_enabled(void) __attribute__((always_inline));
static __inline int get_hw_coreid(uint32_t coreid);
static __inline int hw_core_id(void) __attribute__((always_inline));
static __inline int get_os_coreid(int hw_coreid);
static __inline int core_id(void) __attribute__((always_inline));
static __inline void cache_flush(void) __attribute__((always_inline));
static __inline void reboot(void) __attribute__((always_inline)) __attribute__((noreturn));

/* in trap.c */
void send_ipi(uint32_t os_coreid, uint8_t vector);
/* in cpuinfo.c */
void print_cpuinfo(void);
void show_mapping(uintptr_t start, size_t size);

/* declared in smp.c */
int hw_coreid_lookup[MAX_NUM_CPUS];
int os_coreid_lookup[MAX_NUM_CPUS];

static __inline void
breakpoint(void)
{
	__asm __volatile("int3");
}

static __inline void 
invlpg(void *addr)
{ 
	__asm __volatile("invlpg (%0)" : : "r" (addr) : "memory");
}  

static __inline void
tlbflush(void)
{
	uint32_t cr3;
	__asm __volatile("movl %%cr3,%0" : "=r" (cr3));
	__asm __volatile("movl %0,%%cr3" : : "r" (cr3));
}

static __inline void
icache_flush_page(void* va, void* kva)
{
	// x86 handles self-modifying code (mostly) without SW support
}

static __inline uint64_t
read_tsc(void)
{
	uint64_t tsc;
	__asm __volatile("rdtsc" : "=A" (tsc));
	return tsc;
}

/* non-core-id reporting style (it is in ecx) */
static __inline uint64_t
read_tscp(void)
{
	uint64_t tsc;
	__asm __volatile("rdtscp" : "=A" (tsc) : : "ecx");
	return tsc;
}

/* Check out k/a/x86/rdtsc_test.c for more info */
static __inline uint64_t 
read_tsc_serialized(void)
{
	asm volatile("lfence");	/* mfence on amd */
	return read_tsc();
}

static __inline void
enable_irq(void)
{
	asm volatile("sti");
}

static __inline void
disable_irq(void)
{
	asm volatile("cli");
}

static __inline void
enable_irqsave(int8_t* state)
{
	// *state tracks the number of nested enables and disables
	// initial value of state: 0 = first run / no favorite
	// > 0 means more enabled calls have been made
	// < 0 means more disabled calls have been made
	// Mostly doing this so we can call disable_irqsave first if we want

	// one side or another "gets a point" if interrupts were already the
	// way it wanted to go.  o/w, state stays at 0.  if the state was not 0
	// then, enabling/disabling isn't even an option.  just increment/decrement

	// if enabling is winning or tied, make sure it's enabled
	if ((*state == 0) && !irq_is_enabled())
		enable_irq();
	else
		(*state)++;
}

static __inline void
disable_irqsave(int8_t* state)
{
	if ((*state == 0) && irq_is_enabled())
		disable_irq();
	else 
		(*state)--;
}

static __inline void
cpu_relax(void)
{
	__cpu_relax();
}

/* This doesn't atomically enable interrupts and then halt, like we want, so
 * x86 needs to use a custom helper in the irq handler in trap.c. */
static __inline void
cpu_halt(void)
{
	asm volatile("sti; hlt" : : : "memory");
}

static __inline void
clflush(uintptr_t* addr)
{
	asm volatile("clflush %0" : : "m"(*addr));
}

static __inline int
irq_is_enabled(void)
{
	return read_eflags() & FL_IF;
}

/* os_coreid -> hw_coreid */
static __inline int
get_hw_coreid(uint32_t coreid)
{
	return hw_coreid_lookup[coreid];
}

static __inline int
hw_core_id(void)
{
	return lapic_get_id();
}

/* hw_coreid -> os_coreid */
static __inline int
get_os_coreid(int hw_coreid)
{
	return os_coreid_lookup[hw_coreid];
}

/* core_id() returns the OS core number, not to be confused with the
 * hardware-specific core identifier (such as the lapic id) returned by
 * hw_core_id() */
static __inline int
core_id(void)
{
	return get_os_coreid(hw_core_id());
}

static __inline void
cache_flush(void)
{
	wbinvd();
}

static __inline void
reboot(void)
{
	outb(0x92, 0x3);
	asm volatile ("movl $0, %esp; int $0");
	while(1);
}

#endif /* !ROS_INC_ARCH_H */
#include <parlib.h>
#include <ros/mman.h>
#include <ros/resource.h>
#include <ros/procdata.h>
#include <ros/event.h>
#include <ros/bcq.h>
#include <arch/arch.h>
#include <stdio.h>
#include <vcore.h>
#include <mcs.h>
#include <timing.h>
#include <rassert.h>
#include <event.h>
#include <uthread.h>

void ghetto_vcore_entry(void);

struct schedule_ops ghetto_sched_ops = {
	.sched_entry = ghetto_vcore_entry,
};
struct schedule_ops *sched_ops = &ghetto_sched_ops;

/* All MCP syscalls will spin instead of blocking */
static void __ros_syscall_spinon(struct syscall *sysc)
{
	while (!(atomic_read(&sysc->flags) & (SC_DONE | SC_PROGRESS)))
		cpu_relax();
}

int main(int argc, char** argv)
{
	uint32_t vcoreid;
	int nr_vcores;

	if (argc < 2)
		nr_vcores = max_vcores();
	else
		nr_vcores = atoi(argv[1]);

	/* Inits a thread for us, though we won't use it.  Just a hack to get into
	 * _M mode.  Note this requests one vcore for us */
	struct uthread dummy = {0};
	uthread_lib_init(&dummy);

	/* Reset the blockon to be the spinner...  This is really shitty.  Any
	 * blocking calls after we become an MCP and before this will fail.  This is
	 * just mhello showing its warts due to trying to work outside uthread.c */
	ros_syscall_blockon = __ros_syscall_spinon;

	vcore_request(nr_vcores - 1); /* since we already have 1 */

	while (1)
		sys_halt_core(0);

	return 0;
}

void ghetto_vcore_entry(void)
{
	if (vcore_id() == 0)
		run_current_uthread();

	while (1)
		sys_halt_core(0);
}

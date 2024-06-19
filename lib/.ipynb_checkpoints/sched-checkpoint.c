#include <env.h>
#include <pmap.h>
#include <printf.h>

/* Overview:
 *  Implement simple round-robin scheduling.
 *  Search through 'envs' for a runnable environment,
 *  in circular fashion starting after the previously running env,
 *  and switch to the first such environment found.
 *
 * Hints:
 *  The variable which is for counting should be defined as 'static'.
 */

void sched_yield(void)
{
    static int count = 0;
    struct Env *e;

    // 检查当前环境的时间片是否用完
    if (curenv) {
        if (curenv->env_runs > 0) {
            curenv->env_runs--;
            return;
        } else {
            curenv->env_status = ENV_RUNNABLE;
            LIST_INSERT_HEAD(&env_sched_list[1], curenv, env_sched_link);
        }
    }

    // 从 env_sched_list[0] 中调度下一个就绪环境
    while (1) {
        // 如果 env_sched_list[0] 为空，切换到 env_sched_list[1]
        if (LIST_EMPTY(&env_sched_list[0])) {
            LIST_INIT(&env_sched_list[0]);
            while (!LIST_EMPTY(&env_sched_list[1])) {
                e = LIST_FIRST(&env_sched_list[1]);
                LIST_REMOVE(e, env_sched_link);
                LIST_INSERT_HEAD(&env_sched_list[0], e, env_sched_link);
            }
        }

        // 尝试调度 env_sched_list[0] 中的第一个环境
        if (!LIST_EMPTY(&env_sched_list[0])) {
            e = LIST_FIRST(&env_sched_list[0]);
            LIST_REMOVE(e, env_sched_link);
            curenv = e;
            curenv->env_runs = curenv->env_pri - 1; // 设置时间片
            env_run(curenv);
        }

        // 没有可运行的环境，陷入死循环等待
        while (1);
    }
}

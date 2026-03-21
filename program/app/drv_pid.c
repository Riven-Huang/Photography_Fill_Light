#include "drv_pid.h"

#define DRV_PID_Q15_SHIFT    15
#define DRV_PID_Q15_HALF     (1L << (DRV_PID_Q15_SHIFT - 1))

static int32_t drv_pid_clamp_s32(int32_t value, int32_t min_value, int32_t max_value)
{
    if (value < min_value) {
        return min_value;
    }

    if (value > max_value) {
        return max_value;
    }

    return value;
}

static int32_t drv_pid_s32_to_q15(int32_t value)
{
    return value << DRV_PID_Q15_SHIFT;
}

static int32_t drv_pid_q15_to_s32_round(int32_t value_q15)
{
    if (value_q15 >= 0) {
        return (value_q15 + DRV_PID_Q15_HALF) >> DRV_PID_Q15_SHIFT;
    }

    return -(((-value_q15) + DRV_PID_Q15_HALF) >> DRV_PID_Q15_SHIFT);
}

/* PI控制器初始化；输入输出量纲必须在上层先统一好。 */
void drv_pid_pi_init(drv_pid_pi_t *pid,
                     int32_t kp_q15,
                     int32_t ki_q15,
                     int32_t out_min,
                     int32_t out_max,
                     int32_t out_init)
{
    if (pid == 0) {
        return;
    }

    pid->kp_q15 = kp_q15;
    pid->ki_q15 = ki_q15;
    pid->out_min = out_min;
    pid->out_max = out_max;
    pid->ref = 0;
    pid->feedback = 0;
    pid->error = 0;
    pid->p_out = 0;

    drv_pid_pi_reset(pid, out_init);
}

/* 复位积分项；适合模式切换或故障恢复后重新接管。 */
void drv_pid_pi_reset(drv_pid_pi_t *pid, int32_t out_init)
{
    if (pid == 0) {
        return;
    }

    out_init = drv_pid_clamp_s32(out_init, pid->out_min, pid->out_max);
    pid->ref = 0;
    pid->feedback = 0;
    pid->error = 0;
    pid->p_out = 0;
    pid->output = out_init;
    pid->i_out = out_init;
    pid->i_term_q15 = drv_pid_s32_to_q15(out_init);
}

/* 执行一次PI更新；ref与feedback量纲必须一致，运行结果会回写到结构体里。 */
int32_t drv_pid_pi_step(drv_pid_pi_t *pid, int32_t ref, int32_t feedback)
{
    int32_t p_term_q15;
    int32_t i_candidate_q15;
    int32_t out_q15;
    int32_t out_s32;
    int32_t i_min_q15;
    int32_t i_max_q15;

    if (pid == 0) {
        return 0;
    }

    pid->ref = ref;
    pid->feedback = feedback;
    pid->error = ref - feedback;

    p_term_q15 = pid->error * pid->kp_q15;
    i_candidate_q15 = pid->i_term_q15 + (pid->error * pid->ki_q15);

    /* 积分项先限在输出物理范围内，防止长时间误差把内部状态冲到不可恢复。 */
    i_min_q15 = drv_pid_s32_to_q15(pid->out_min);
    i_max_q15 = drv_pid_s32_to_q15(pid->out_max);
    i_candidate_q15 = drv_pid_clamp_s32(i_candidate_q15, i_min_q15, i_max_q15);

    out_q15 = p_term_q15 + i_candidate_q15;
    out_s32 = drv_pid_q15_to_s32_round(out_q15);

    if (out_s32 > pid->out_max) {
        out_s32 = pid->out_max;
        if (pid->error < 0) {
            pid->i_term_q15 = i_candidate_q15;
        }
    } else if (out_s32 < pid->out_min) {
        out_s32 = pid->out_min;
        if (pid->error > 0) {
            pid->i_term_q15 = i_candidate_q15;
        }
    } else {
        pid->i_term_q15 = i_candidate_q15;
    }

    pid->p_out = drv_pid_q15_to_s32_round(p_term_q15);
    pid->i_out = drv_pid_q15_to_s32_round(pid->i_term_q15);
    pid->output = out_s32;

    return out_s32;
}

//
// End of File
//

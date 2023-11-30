/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 MediaTek Inc.
*/

#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/semaphore.h>
#include <linux/module.h>
#include <linux/wait.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/ktime.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <uapi/linux/sched/types.h>
#include "ion_drv.h"
#include "mtk_ion.h"
#ifdef CONFIG_MTK_M4U
#include "m4u.h"
#endif
#include "disp_drv_platform.h"
#include "debug.h"
#include "ddp_debug.h"
#include "disp_drv_log.h"
#include "disp_lcm.h"
#include "disp_utils.h"
#include "disp_session.h"
#include "primary_display.h"
#include "disp_helper.h"
#include "cmdq_def.h"
#include "cmdq_record.h"
#include "cmdq_reg.h"
#include "cmdq_core.h"
#include "ddp_manager.h"
#include "disp_lcm.h"
#include "ddp_clkmgr.h"
/* #include "mmdvfs_mgr.h" */
#include "disp_drv_log.h"
#include "ddp_log.h"
#include "disp_lowpower.h"
/* device tree */
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/io.h>
/* #include "mach/eint.h" */
#if defined(CONFIG_MTK_LEGACY)
#include <mach/mtk_gpio.h>
#include <cust_gpio_usage.h>
#include <cust_eint.h>
#else
#include "disp_dts_gpio.h"
#include <linux/gpio.h>
#endif

#include "disp_recovery.h"
#include "disp_partial.h"
#include "ddp_dsi.h"
#include "ddp_disp_bdg.h"

/* For abnormal check */
static struct task_struct *primary_display_check_task;
/* used for blocking check task  */
static wait_queue_head_t _check_task_wq;
/* For  Check Task */
static atomic_t _check_task_wakeup = ATOMIC_INIT(0);

/* For EXT TE EINT Check */
static wait_queue_head_t esd_ext_te_wq;
/* For EXT TE EINT Check */
static atomic_t esd_ext_te_event = ATOMIC_INIT(0);
static unsigned int esd_check_mode;
static unsigned int esd_check_enable;
unsigned int esd_checking;
static int te_irq;

#if defined(CONFIG_MTK_DUAL_DISPLAY_SUPPORT) && \
	(CONFIG_MTK_DUAL_DISPLAY_SUPPORT == 2)
/***********external display dual LCM ESD check******************/
/* For abnormal check */
static struct task_struct *external_display_check_task;
/* used for blocking check task  */
static wait_queue_head_t extd_check_task_wq;
/* For  Check Task */
static atomic_t extd_check_task_wakeup = ATOMIC_INIT(0);

/* For EXT TE EINT Check */
static wait_queue_head_t esd_ext_te_1_wq;
/* For EXT TE EINT Check */
static atomic_t esd_ext_te_1_event = ATOMIC_INIT(0);
static unsigned int extd_esd_check_mode;
static unsigned int extd_esd_check_enable;
#endif
/* C3T code for HQ-219022 by jiangyue at 2022/08/22 start */
#ifdef CONFIG_MI_ERRFLAG_ESD_CHECK_ENABLE
atomic_t lcm_ready = ATOMIC_INIT(0);
atomic_t lcm_valid_irq = ATOMIC_INIT(0);
#endif
/* C3T code for HQ-219022 by jiangyue at 2022/08/22 end */
/* C3T code for HQ-219022 by sunfeiting at 2022/08/29 start */
bool esd_flag;
/* C3T code for HQ-219022 by sunfeiting at 2022/08/29 end */

unsigned int get_esd_check_mode(void)
{
	return esd_check_mode;
}

void set_esd_check_mode(unsigned int mode)
{
	esd_check_mode = mode;
}

unsigned int _can_switch_check_mode(void)
{
	int ret = 0;
	struct LCM_PARAMS *params;

	params = primary_get_lcm()->params;
	if (params->dsi.customization_esd_check_enable == 0 &&
	    params->dsi.lcm_esd_check_table[0].cmd != 0 &&
	    disp_helper_get_option(DISP_OPT_ESD_CHECK_SWITCH))
		ret = 1;
	return ret;
}

unsigned int _need_do_esd_check(void)
{
	int ret = 0;

#ifdef CONFIG_OF
	if ((primary_get_lcm()->params->dsi.esd_check_enable == 1) &&
		(islcmconnected == 1))
		ret = 1;
#else
	if (primary_get_lcm()->params->dsi.esd_check_enable == 1)
		ret = 1;
#endif
	return ret;
}

/**
 * For Cmd Mode Read LCM Check
 * Config cmdq_handle_config_esd
 */
int _esd_check_config_handle_cmd(struct cmdqRecStruct *qhandle)
{
	int ret = 0; /* 0:success */
	disp_path_handle phandle = primary_get_dpmgr_handle();

	/* 1.reset */
	cmdqRecReset(qhandle);

	primary_display_manual_lock();

	/* 2.write first instruction */
	/* cmd mode: wait CMDQ_SYNC_TOKEN_STREAM_EOF
	 * (wait trigger thread done)
	 */
	cmdqRecWaitNoClear(qhandle, CMDQ_SYNC_TOKEN_STREAM_EOF);

	/* 3.clear CMDQ_SYNC_TOKEN_ESD_EOF
	 * (trigger thread need wait this sync token)
	 */
	cmdqRecClearEventToken(qhandle, CMDQ_SYNC_TOKEN_ESD_EOF);

	/* 4.write instruction(read from lcm) */
	dpmgr_path_build_cmdq(phandle, qhandle, CMDQ_ESD_CHECK_READ, 0);

	/* 5.set CMDQ_SYNC_TOKE_ESD_EOF(trigger thread can work now) */
	cmdqRecSetEventToken(qhandle, CMDQ_SYNC_TOKEN_ESD_EOF);

	primary_display_manual_unlock();

	/* 6.flush instruction */
	dprec_logger_start(DPREC_LOGGER_ESD_CMDQ, 0, 0);
	ret = cmdqRecFlush(qhandle);
	dprec_logger_done(DPREC_LOGGER_ESD_CMDQ, 0, 0);

	DISPINFO("[ESD]%s ret=%d\n", __func__, ret);

	if (ret)
		ret = 1;
	return ret;
}

/**
 * For Vdo Mode Read LCM Check
 * Config cmdq_handle_config_esd
 * return value: 0:success, 1:fail
 */
int _esd_check_config_handle_vdo(struct cmdqRecStruct *qhandle)
{
	int ret = 0;
	disp_path_handle phandle = primary_get_dpmgr_handle();

	/* 1.reset */
	cmdqRecReset(qhandle);
	/*set esd check read timeout 200ms*/
	/*remove to dts*/
	/*cmdq_task_set_timeout(qhandle, 200);*/
	/* wait stream eof first */
	/* cmdqRecWait(qhandle, CMDQ_EVENT_DISP_RDMA0_EOF); */

#ifdef CONFIG_MTK_MT6382_BDG
	cmdqRecClearEventToken(qhandle, CMDQ_EVENT_DSI_TE);
#endif
	cmdqRecWait(qhandle, CMDQ_EVENT_MUTEX0_STREAM_EOF);

	primary_display_manual_lock();

	esd_checking = 1;

	/* 2.stop dsi vdo mode */
	dpmgr_path_build_cmdq(phandle, qhandle, CMDQ_STOP_VDO_MODE, 0);

	/* 3.write instruction(read from lcm) */
	dpmgr_path_build_cmdq(phandle, qhandle, CMDQ_ESD_CHECK_READ, 0);

	/* 4.start dsi vdo mode */
	dpmgr_path_build_cmdq(phandle, qhandle, CMDQ_START_VDO_MODE, 0);
	cmdqRecClearEventToken(qhandle, CMDQ_EVENT_MUTEX0_STREAM_EOF);
	/* cmdqRecClearEventToken(qhandle, CMDQ_EVENT_DISP_RDMA0_EOF); */
	/* 5.trigger path */
	dpmgr_path_trigger(phandle, qhandle, CMDQ_ENABLE);

	/* mutex sof wait*/
	ddp_mutex_set_sof_wait(dpmgr_path_get_mutex(phandle), qhandle, 0);

	/* 6.flush instruction */
	dprec_logger_start(DPREC_LOGGER_ESD_CMDQ, 0, 0);
	ret = cmdqRecFlush(qhandle);
	dprec_logger_done(DPREC_LOGGER_ESD_CMDQ, 0, 0);
	DISPINFO("[ESD]%s ret=%d\n", __func__, ret);
	primary_display_manual_unlock();

	if (ret)
		ret = 1;
	return ret;
}

/* For EXT TE EINT Check */
static irqreturn_t _esd_check_ext_te_irq_handler(int irq, void *data)
{
/* C3T code for HQ-219022 by jiangyue at 2022/08/22 start */
#ifdef CONFIG_MI_ERRFLAG_ESD_CHECK_ENABLE
	DISPCHECK("[ESD] _esd_check_ext_te_irq_handler start");
	mmprofile_log_ex(ddp_mmp_get_events()->esd_vdo_eint,
		MMPROFILE_FLAG_PULSE, 0, 0);
	if(atomic_read(&lcm_ready)){
		if(atomic_read(&lcm_valid_irq)){
			atomic_set(&lcm_valid_irq, 0);
			DISPCHECK("[ESD]%s   invalid irq, skip\n", __func__);
		}
		else{
			atomic_set(&esd_ext_te_event, 1);
			DISPCHECK("[ESD]%s\n", __func__);
			wake_up_interruptible(&esd_ext_te_wq);
		}
	}
	else{
		atomic_set(&lcm_valid_irq, 1);
		DISPCHECK("[ESD] _esd_check_ext_te_irq_handler lcm not ready, skip");
	}
#else
	mmprofile_log_ex(ddp_mmp_get_events()->esd_vdo_eint,
		MMPROFILE_FLAG_PULSE, 0, 0);
	atomic_set(&esd_ext_te_event, 1);
	DISPINFO("[ESD]%s\n", __func__);
	wake_up_interruptible(&esd_ext_te_wq);
#endif
/* C3T code for HQ-219022 by jiangyue at 2022/08/22 end */
	return IRQ_HANDLED;
}

void primary_display_switch_esd_mode(int mode)
{
	if (mode == GPIO_EINT_MODE) {
		/* Enable TE EINT */
		enable_irq(te_irq);

	} else if (mode == GPIO_DSI_MODE) {
		/* Disable TE EINT */
		disable_irq(te_irq);
	}
}

int do_esd_check_eint(void)
{
/* C3T code for HQ-219022 by jiangyue at 2022/08/22 start */
#ifdef CONFIG_MI_ERRFLAG_ESD_CHECK_ENABLE
	int ret = 0;
	DISPCHECK("[ESD]ESD check eint\n");
	enable_irq(te_irq);
	if (wait_event_interruptible_timeout(esd_ext_te_wq,
		atomic_read(&esd_ext_te_event), HZ / 2) > 0)
		ret = 1; /* esd check fail */
	else
		ret = 0; /* esd check pass */
	atomic_set(&esd_ext_te_event, 0);
	disable_irq(te_irq);
#else

	int ret = 0;
	mmp_event mmp_te = ddp_mmp_get_events()->esd_extte;

	DISPINFO("[ESD]ESD check eint\n");
	mmprofile_log_ex(mmp_te, MMPROFILE_FLAG_PULSE,
		primary_display_is_video_mode(), GPIO_EINT_MODE);
	primary_display_switch_esd_mode(GPIO_EINT_MODE);

	if (wait_event_interruptible_timeout(esd_ext_te_wq,
		atomic_read(&esd_ext_te_event), HZ / 2) > 0)
		ret = 0; /* esd check pass */
	else
		ret = 1; /* esd check fail */

	atomic_set(&esd_ext_te_event, 0);

	primary_display_switch_esd_mode(GPIO_DSI_MODE);
#endif
/* C3T code for HQ-219022 by jiangyue at 2022/08/22 end */
	return ret;
}

int do_esd_check_dsi_te(void)
{
	int ret = 0;

	if (dpmgr_wait_event_timeout(primary_get_dpmgr_handle(),
		DISP_PATH_EVENT_IF_VSYNC, HZ / 2) > 0)
		ret = 0; /* esd check pass */
	else
		ret = 1; /* esd check fail */

	return ret;
}

int do_esd_check_read(void)
{
	int ret = 0;
	struct cmdqRecStruct *qhandle = NULL;
	disp_path_handle phandle = primary_get_dpmgr_handle();
	mmp_event mmp_te = ddp_mmp_get_events()->esd_extte;

	DISPCHECK("[ESD]ESD check read\n");
	mmprofile_log_ex(mmp_te, MMPROFILE_FLAG_PULSE,
		primary_display_is_video_mode(), GPIO_DSI_MODE);

	/* only cmd mode read & with disable mmsys clk will kick */
	if ((disp_helper_get_option(DISP_OPT_IDLEMGR_ENTER_ULPS) &&
	    !primary_display_is_video_mode()))
		primary_display_idlemgr_kick((char *)__func__, 1);

	/* 0.create esd check cmdq */
	ret = cmdqRecCreate(CMDQ_SCENARIO_DISP_ESD_CHECK, &qhandle);
	if (ret) {
		DISPERR("%s:%d, create cmdq handle fail!ret=%d\n",
			__func__, __LINE__, ret);
		return -1;
	}
	cmdqRecReset(qhandle);

	primary_display_manual_lock();
	dpmgr_path_build_cmdq(phandle, qhandle, CMDQ_ESD_ALLC_SLOT, 0);
	primary_display_manual_unlock();

	/* 1.use cmdq to read from lcm */
	if (primary_display_is_video_mode())
		ret = _esd_check_config_handle_vdo(qhandle);
	else
		ret = _esd_check_config_handle_cmd(qhandle);

	primary_display_manual_lock();

	if (ret == 1) {	/* cmdq fail */
		if (need_wait_esd_eof()) {
			/* Need set esd check eof synctoken to
			 * let trigger loop go.
			 */
			cmdqCoreSetEvent(CMDQ_SYNC_TOKEN_ESD_EOF);
		}
		/* do dsi reset */
		dpmgr_path_build_cmdq(phandle, qhandle, CMDQ_DSI_RESET, 0);
		goto destroy_cmdq;
	}

	/* 2.check data(*cpu check now) */
	ret = dpmgr_path_build_cmdq(phandle, qhandle, CMDQ_ESD_CHECK_CMP, 0);
	if (ret)
		ret = 1; /* esd check fail */

destroy_cmdq:
	dpmgr_path_build_cmdq(phandle, qhandle, CMDQ_ESD_FREE_SLOT, 0);

	primary_display_manual_unlock();

	/* 3.destroy esd config thread */
	cmdqRecDestroy(qhandle);

	return ret;
}

int do_lcm_vdo_lp_read(struct ddp_lcm_read_cmd_table *read_table)
{
	int ret = 0;
	int i = 0;
	struct cmdqRecStruct *handle;
	static cmdqBackupSlotHandle read_Slot[2] = {0, 0};
	int h = 0;

	primary_display_manual_lock();

	if (primary_get_state() == DISP_SLEPT) {
		DISPINFO("primary display path is slept?? -- skip read\n");
		primary_display_manual_unlock();
		return -1;
	}

	/* 0.create esd check cmdq */
	cmdqRecCreate(CMDQ_SCENARIO_DISP_ESD_CHECK, &handle);
	for (h = 0; h < 2; h++) {
		cmdqBackupAllocateSlot(&read_Slot[h], 3);
		for (i = 0; i < 3; i++)
			cmdqBackupWriteSlot(read_Slot[h], i, 0xff00ff00);
	}
	/* 1.use cmdq to read from lcm */
	if (primary_display_is_video_mode()) {

		/* 1.reset */
		cmdqRecReset(handle);

		/* wait stream eof first */
		/*cmdqRecWait(handle, CMDQ_EVENT_DISP_RDMA0_EOF);*/
		cmdqRecWait(handle, CMDQ_EVENT_MUTEX0_STREAM_EOF);

		/* 2.stop dsi vdo mode */
		dpmgr_path_build_cmdq(primary_get_dpmgr_handle(),
		handle, CMDQ_STOP_VDO_MODE, 0);

		/* 3.read from lcm */
		ddp_dsi_read_lcm_cmdq(DISP_MODULE_DSI0,
		read_Slot, handle, read_table);

		/* 4.start dsi vdo mode */
		dpmgr_path_build_cmdq(primary_get_dpmgr_handle(),
		handle, CMDQ_START_VDO_MODE, 0);

		cmdqRecClearEventToken(handle, CMDQ_EVENT_MUTEX0_STREAM_EOF);

		/* 5. trigger path */
		dpmgr_path_trigger(primary_get_dpmgr_handle(),
							handle, CMDQ_ENABLE);

		/*	mutex sof wait*/
		ddp_mutex_set_sof_wait(
		dpmgr_path_get_mutex(primary_get_dpmgr_handle()),
		handle, 0);

		/* 6.flush instruction */
		ret = cmdqRecFlush(handle);

	} else {
		DISPINFO("Not support cmd mode\n");
	}

	if (ret == 1) {	/* cmdq fail */
		if (need_wait_esd_eof()) {
			/* Need set esd check eof */
			/*synctoken to let trigger loop go. */
			cmdqCoreSetEvent(CMDQ_SYNC_TOKEN_ESD_EOF);
		}
		/* do dsi reset */
		dpmgr_path_build_cmdq(primary_get_dpmgr_handle(), handle,
			 CMDQ_DSI_RESET, 0);
		goto DISPTORY;
	}

	for (i = 0; i < 3; i++) {
		cmdqBackupReadSlot(read_Slot[0], i,
			(uint32_t *)&read_table->data[i]);

		cmdqBackupReadSlot(read_Slot[1], i,
			(uint32_t *)&read_table->data1[i]);

		DISPERR("%s: read_table->data1[%d] byte0~1=0x%x~0x%x\n",
			__func__, i,
			read_table->data[i].byte0, read_table->data[i].byte1);
		DISPERR("%s: read_table->data1[%d] byte2~3=0x%x~0x%x\n",
			__func__, i,
			read_table->data[i].byte2, read_table->data[i].byte3);
		DISPERR("%s: read_table->data1[%d] byte0~1=0x%x~0x%x\n",
			__func__, i,
			read_table->data1[i].byte0, read_table->data1[i].byte1);
		DISPERR("%s: read_table->data1[%d] byte2~3=0x%x~0x%x\n",
			__func__, i,
			read_table->data1[i].byte2, read_table->data1[i].byte3);
	}

DISPTORY:
	for (h = 0; h < 2; h++) {
		if (read_Slot[h]) {
			cmdqBackupFreeSlot(read_Slot[h]);
			read_Slot[h] = 0;
		}
	}
	/* 7.destroy esd config thread */
	cmdqRecDestroy(handle);
	primary_display_manual_unlock();

	return ret;
}

int do_lcm_vdo_lp_write(struct ddp_lcm_write_cmd_table *write_table,
			unsigned int count)
{
	int ret = 0;
	int i = 0;
	struct cmdqRecStruct *handle;

	primary_display_manual_lock();

	if (primary_get_state() == DISP_SLEPT) {
		DISPINFO("primary display path is slept?? -- skip read\n");
		primary_display_manual_unlock();
		return -1;
	}

	/* 0.create esd check cmdq */
	cmdqRecCreate(CMDQ_SCENARIO_DISP_ESD_CHECK, &handle);

	/* 1.use cmdq to read from lcm */
	if (primary_display_is_video_mode()) {

		/* 1.reset */
		cmdqRecReset(handle);

		/* wait stream eof first */
		cmdqRecWait(handle, CMDQ_EVENT_MUTEX0_STREAM_EOF);

		/* 2.stop dsi vdo mode */
		dpmgr_path_build_cmdq(primary_get_dpmgr_handle(),
				handle, CMDQ_STOP_VDO_MODE, 0);

		/* 3.write instruction */
		for (i = 0; i < count; i++) {
			ret = ddp_dsi_write_lcm_cmdq(DISP_MODULE_DSI0,
			handle, write_table[i].cmd,
			write_table[i].count,
			write_table[i].para_list);
			if (ret)
				break;
		}

		/* 4.start dsi vdo mode */
		dpmgr_path_build_cmdq(primary_get_dpmgr_handle(),
		handle, CMDQ_START_VDO_MODE, 0);

		cmdqRecClearEventToken(handle, CMDQ_EVENT_MUTEX0_STREAM_EOF);

		/* 5. trigger path */
		dpmgr_path_trigger(primary_get_dpmgr_handle(),
		handle, CMDQ_ENABLE);

		/*	mutex sof wait*/
		ddp_mutex_set_sof_wait(dpmgr_path_get_mutex(
		primary_get_dpmgr_handle()), handle, 0);


		/* 6.flush instruction */
		ret = cmdqRecFlush(handle);
	} else {
		DISPINFO("Not support cmd mode\n");
	}

	if (ret == 1) {	/* cmdq fail */
		if (need_wait_esd_eof()) {
			/* Need set esd check eof */
			/*synctoken to let trigger loop go. */
			cmdqCoreSetEvent(CMDQ_SYNC_TOKEN_ESD_EOF);
		}
		/* do dsi reset */
		dpmgr_path_build_cmdq(primary_get_dpmgr_handle(), handle,
			CMDQ_DSI_RESET, 0);
		goto DISPTORY;
	}

DISPTORY:
	/* 7.destroy esd config thread */
	cmdqRecDestroy(handle);
	primary_display_manual_unlock();

	return ret;
}

/**
 * ESD CHECK FUNCTION
 * return 1: esd check fail
 * return 0: esd check pass
 */
int primary_display_esd_check(void)
{
	int ret = 0;
/* C3T code for HQ-219022 by jiangyue at 2022/08/22 start */
#ifndef CONFIG_MI_ERRFLAG_ESD_CHECK_ENABLE
	unsigned int mode;
#endif
/* C3T code for HQ-219022 by jiangyue at 2022/08/22 end */
	mmp_event mmp_te = ddp_mmp_get_events()->esd_extte;
	mmp_event mmp_rd = ddp_mmp_get_events()->esd_rdlcm;
	mmp_event mmp_chk = ddp_mmp_get_events()->esd_check_t;
	struct LCM_PARAMS *params;

	dprec_logger_start(DPREC_LOGGER_ESD_CHECK, 0, 0);
	mmprofile_log_ex(mmp_chk, MMPROFILE_FLAG_START, 0, 0);
	DISPCHECK("[ESD]ESD check begin\n");

	primary_display_manual_lock();
	if (primary_get_state() == DISP_SLEPT) {
		mmprofile_log_ex(mmp_chk, MMPROFILE_FLAG_PULSE, 1, 0);
		DISPCHECK("[ESD]Primary DISP slept. Skip esd check\n");
		primary_display_manual_unlock();
		goto done;
	}
	primary_display_manual_unlock();

	/* Esd Check: EXT TE */
	params = primary_get_lcm()->params;
	if (params->dsi.customization_esd_check_enable == 0) {
		/* use TE for esd check */
		mmprofile_log_ex(mmp_te, MMPROFILE_FLAG_START, 0, 0);
/* C3T code for HQ-219022 by jiangyue at 2022/08/22 start */
#ifndef CONFIG_MI_ERRFLAG_ESD_CHECK_ENABLE
		if (primary_display_is_video_mode()) {
			mode = get_esd_check_mode();
			if (mode == GPIO_EINT_MODE) {
				ret = do_esd_check_eint();
				if (_can_switch_check_mode())
					set_esd_check_mode(GPIO_DSI_MODE);
			} else {
				ret = do_esd_check_read();
				if (_can_switch_check_mode())
					set_esd_check_mode(GPIO_EINT_MODE);
			}
		} else
			ret = do_esd_check_eint();
#else
		ret = do_esd_check_eint();
		DISPCHECK("[ESD]disp_lcm_esd_check_eint--------ret=%d\n",ret);
#endif
/* C3T code for HQ-219022 by jiangyue at 2022/08/22 end */
		mmprofile_log_ex(mmp_te, MMPROFILE_FLAG_END, 0, ret);

		goto done;
	}

	/* Esd Check: Read from lcm */
	mmprofile_log_ex(mmp_rd, MMPROFILE_FLAG_START,
			 0, primary_display_cmdq_enabled());

	if (primary_display_cmdq_enabled() == 0) {
		DISPCHECK("[ESD]not support cpu read do esd check\n");
		mmprofile_log_ex(mmp_rd, MMPROFILE_FLAG_END, 0, ret);
		goto done;
	}

	mmprofile_log_ex(mmp_rd, MMPROFILE_FLAG_PULSE,
			 0, primary_display_is_video_mode());

	/* only cmd mode read & with disable mmsys clk will kick */
	ret = do_esd_check_read();

	mmprofile_log_ex(mmp_rd, MMPROFILE_FLAG_END, 0, ret);

done:
	if (ret)
		DISPERR("[ESD]ESD check fail!\n");
	else
		DISPINFO("[ESD]ESD check pass!\n");

	mmprofile_log_ex(mmp_chk, MMPROFILE_FLAG_END, 0, ret);
	dprec_logger_done(DPREC_LOGGER_ESD_CHECK, 0, 0);
	return ret;
}

static int primary_display_check_recovery_worker_kthread(void *data)
{
	struct sched_param param = {.sched_priority = 87 };
	int ret = 0;
	int i = 0;
	int esd_try_cnt = 5; /* 20; */
/* C3T code for HQ-219022 by jiangyue at 2022/08/22 start */
#ifndef CONFIG_MI_ERRFLAG_ESD_CHECK_ENABLE
	int recovery_done = 0;
/* C3T code for HQ-219022 by sunfeiting at 2022/08/29 start */
	esd_flag = false;
/* C3T code for HQ-219022 by sunfeiting at 2022/08/29 end */
#endif

	sched_setscheduler(current, SCHED_RR, &param);
#ifndef CONFIG_MI_ERRFLAG_ESD_CHECK_ENABLE
/* C3T code for HQ-219022 by jiangyue at 2022/08/22 end */
	while (1) {
		msleep(2000); /* 2s */
		ret = wait_event_interruptible(_check_task_wq,
			atomic_read(&_check_task_wakeup));
		if (ret < 0) {
			DISPINFO("[ESD]check thread waked up accidently\n");
			continue;
		}

		_primary_path_switch_dst_lock();

		/* 1. esd check & recovery */
		if (!esd_check_enable) {
			_primary_path_switch_dst_unlock();
			continue;
		}

		primary_display_manual_lock();
		/* thread relase CPU, when display is slept */
		if (primary_get_state() == DISP_SLEPT) {
			primary_display_manual_unlock();
			_primary_path_switch_dst_unlock();
			primary_display_wait_not_state(DISP_SLEPT,
				MAX_SCHEDULE_TIMEOUT);
			continue;
		}
		primary_display_manual_unlock();

		i = 0; /* repeat */
		do {
			ret = primary_display_esd_check();
			if (!ret) /* success */
				break;

			DISPERR(
				"[ESD]esd check fail, will do esd recovery. try=%d\n",
				i);
			primary_display_esd_recovery();
			recovery_done = 1;
		} while (++i < esd_try_cnt);

		if (ret == 1) {
			DISPERR(
				"[ESD]LCM recover fail. Try time:%d. Disable esd check\n",
				esd_try_cnt);
			primary_display_esd_check_enable(0);
		} else if (recovery_done == 1) {
			DISPCHECK("[ESD]esd recovery success\n");
			recovery_done = 0;
		}
		esd_checking = 0;
		_primary_path_switch_dst_unlock();

		/* 2. other check & recovery */

		if (kthread_should_stop())
			break;
	}
/* C3T code for HQ-219022 by jiangyue at 2022/08/22 start */
#else
	while (1) {
next:		if(!atomic_read(&lcm_ready)){
			msleep(200);
			continue;
		}
		DISPINFO("[ESD] primary_display_check_recovery_worker_kthread start 2");
		i = 0; /* repeat */
		do {
			DISPINFO("[ESD] do_esd_check_eint start");
			ret = do_esd_check_eint();
			if (!ret) /* success */
				break;
			DISPERR(
				"[ESD]esd check fail, will do esd recovery. try=%d\n",
				i);
		/* C3T code for HQ-219022 by sunfeiting at 2022/08/29 start */
			esd_flag = true;
			DISPERR("[ESD]Now esd_flag = %d\n",esd_flag);
		/* C3T code for HQ-219022 by sunfeiting at 2022/08/29 end */
			primary_display_esd_recovery();
			goto next;
		} while (++i < esd_try_cnt);
	}
#endif
/* C3T code for HQ-219022 by jiangyue at 2022/08/22 end */
	return 0;
}

/* ESD RECOVERY */
int primary_display_esd_recovery(void)
{
	enum DISP_STATUS ret = DISP_STATUS_OK;
	struct LCM_PARAMS *lcm_param = NULL;
	mmp_event mmp_r = ddp_mmp_get_events()->esd_recovery_t;

	DISPFUNC();
	dprec_logger_start(DPREC_LOGGER_ESD_RECOVERY, 0, 0);
	mmprofile_log_ex(mmp_r, MMPROFILE_FLAG_START, 0, 0);
	DISPCHECK("[ESD]ESD recovery begin\n");
/* C3T code for HQ-219022 by jiangyue at 2022/08/22 start */
#ifdef CONFIG_MI_ERRFLAG_ESD_CHECK_ENABLE
	atomic_set(&lcm_ready, 0);
	DISPERR("[ESD] atomic_set(&lcm_ready, 0)\n");
#endif
/* C3T code for HQ-219022 by jiangyue at 2022/08/22 end */
	primary_display_manual_lock();
	mmprofile_log_ex(mmp_r, MMPROFILE_FLAG_PULSE,
		       primary_display_is_video_mode(), 1);

	lcm_param = disp_lcm_get_params(primary_get_lcm());
	if (primary_get_state() == DISP_SLEPT) {
		DISPCHECK("[ESD]Primary DISP is slept, skip esd recovery\n");
		goto done;
	}

	/* In video mode, recovery don't need kick and blocking flush */
	if (!primary_display_is_video_mode()) {
		primary_display_idlemgr_kick((char *)__func__, 0);
		mmprofile_log_ex(mmp_r, MMPROFILE_FLAG_PULSE, 0, 2);

	}
	/* blocking flush before stop trigger loop */
	_blocking_flush();

	mmprofile_log_ex(mmp_r, MMPROFILE_FLAG_PULSE, 0, 3);

	DISPINFO("[ESD]display cmdq trigger loop stop[begin]\n");
	_cmdq_stop_trigger_loop();
	DISPINFO("[ESD]display cmdq trigger loop stop[end]\n");

	mmprofile_log_ex(mmp_r, MMPROFILE_FLAG_PULSE, 0, 4);

	DISPDBG("[ESD]stop dpmgr path[begin]\n");
	dpmgr_path_stop(primary_get_dpmgr_handle(), CMDQ_DISABLE);
	DISPCHECK("[ESD]stop dpmgr path[end]\n");
	mmprofile_log_ex(mmp_r, MMPROFILE_FLAG_PULSE, 0, 0xff);

	if (dpmgr_path_is_busy(primary_get_dpmgr_handle())) {
		DISPCHECK("[ESD]primary display path is busy after stop\n");
		dpmgr_wait_event_timeout(primary_get_dpmgr_handle(),
			DISP_PATH_EVENT_FRAME_DONE, HZ * 1);
		DISPCHECK("[ESD]wait frame done ret:%d\n", ret);
	}
	mmprofile_log_ex(mmp_r, MMPROFILE_FLAG_PULSE, 0, 5);

	DISPDBG("[ESD]reset display path[begin]\n");
	dpmgr_path_reset(primary_get_dpmgr_handle(), CMDQ_DISABLE);
	DISPCHECK("[ESD]reset display path[end]\n");

	mmprofile_log_ex(mmp_r, MMPROFILE_FLAG_PULSE, 0, 6);

	DISPDBG("[POWER]lcm suspend[begin]\n");
	/*after dsi_stop, we should enable the dsi basic irq.*/
	dsi_basic_irq_enable(DISP_MODULE_DSI0, NULL);
	disp_lcm_suspend(primary_get_lcm());
	DISPCHECK("[POWER]lcm suspend[end]\n");

	mmprofile_log_ex(mmp_r, MMPROFILE_FLAG_PULSE, 0, 7);

	DISPDBG("[ESD]dsi power reset[begine]\n");
	dpmgr_path_dsi_power_off(primary_get_dpmgr_handle(), NULL);
	if (bdg_is_bdg_connected() == 1) {
		struct disp_ddp_path_config *data_config = NULL;

		bdg_common_deinit(DISP_BDG_DSI0, NULL);

		if (pgc != NULL)
			data_config = dpmgr_path_get_last_config(pgc->dpmgr_handle);
		bdg_common_init(DISP_BDG_DSI0, data_config, NULL);
		mipi_dsi_rx_mac_init(DISP_BDG_DSI0, data_config, NULL);
	}

	dpmgr_path_dsi_power_on(primary_get_dpmgr_handle(), NULL);
	if (!primary_display_is_video_mode())
		dpmgr_path_ioctl(primary_get_dpmgr_handle(), NULL,
				DDP_DSI_ENABLE_TE, NULL);
	dpmgr_path_reset(primary_get_dpmgr_handle(), CMDQ_DISABLE);
	DISPCHECK("[ESD]dsi power reset[end]\n");
	if (bdg_is_bdg_connected() == 1) {
		struct disp_ddp_path_config *data_config = NULL;

//		extern ddp_dsi_config(enum DISP_MODULE_ENUM module,
//		struct disp_ddp_path_config *config, void *cmdq);

		if (pgc != NULL)
			data_config = dpmgr_path_get_last_config(pgc->dpmgr_handle);

		if (data_config != NULL) {
			data_config->dst_dirty = 1;
			dpmgr_path_config(primary_get_dpmgr_handle(), data_config, NULL);
//			ddp_dsi_config(DISP_MODULE_DSI0, data_config, NULL);

			data_config->dst_dirty = 0;
		}
	}
	DISPDBG("[ESD]lcm recover[begin]\n");
	disp_lcm_esd_recover(primary_get_lcm());
	DISPCHECK("[ESD]lcm recover[end]\n");
	mmprofile_log_ex(mmp_r, MMPROFILE_FLAG_PULSE, 0, 8);
	if (bdg_is_bdg_connected() == 1 && get_mt6382_init()) {
		DISPCHECK("set 6382 mode start\n");
		bdg_tx_set_mode(DISP_BDG_DSI0, NULL, get_bdg_tx_mode());
		bdg_tx_start(DISP_BDG_DSI0, NULL);
	}

	DISPDBG("[ESD]start dpmgr path[begin]\n");
	if (disp_partial_is_support()) {
		struct disp_ddp_path_config *data_config =
			dpmgr_path_get_last_config(primary_get_dpmgr_handle());

		primary_display_config_full_roi(data_config,
			primary_get_dpmgr_handle(), NULL);
	}
	dpmgr_path_start(primary_get_dpmgr_handle(), CMDQ_DISABLE);
	DISPCHECK("[ESD]start dpmgr path[end]\n");

	if (dpmgr_path_is_busy(primary_get_dpmgr_handle())) {
		DISPERR("[ESD]Main display busy before triggering SOF\n");
		ret = -1;
		/* goto done; */
	}

	mmprofile_log_ex(mmp_r, MMPROFILE_FLAG_PULSE, 0, 9);
	DISPDBG("[ESD]start cmdq trigger loop[begin]\n");
	_cmdq_start_trigger_loop();
	DISPCHECK("[ESD]start cmdq trigger loop[end]\n");
	mmprofile_log_ex(mmp_r, MMPROFILE_FLAG_PULSE, 0, 10);
	if (primary_display_is_video_mode()) {
		/*
		 * for video mode, we need to force trigger here
		 * for cmd mode, just set DPREC_EVENT_CMDQ_SET_EVENT_ALLOW
		 * when trigger loop start
		 */
		dpmgr_path_trigger(primary_get_dpmgr_handle(), NULL,
			CMDQ_DISABLE);

	}
	mmprofile_log_ex(mmp_r, MMPROFILE_FLAG_PULSE, 0, 11);

	/*
	 * (in suspend) when we stop trigger loop
	 * if no other thread is running, cmdq may disable its clock
	 * all cmdq event will be cleared after suspend
	 */
	cmdqCoreSetEvent(CMDQ_EVENT_DISP_WDMA0_EOF);

	/* set dirty to trigger one frame -- cmd mode */
	if (!primary_display_is_video_mode()) {
		cmdqCoreSetEvent(CMDQ_SYNC_TOKEN_CONFIG_DIRTY);
		mdelay(40);
	}

#ifdef CONFIG_MTK_HIGH_FRAME_RATE
	primary_display_update_cfg_id(0);
	DISPCHECK("%s,cfg_id = 0\n", __func__);
#endif

done:
	primary_display_manual_unlock();
	DISPCHECK("[ESD]ESD recovery end\n");
	mmprofile_log_ex(mmp_r, MMPROFILE_FLAG_END, 0, 0);
	dprec_logger_done(DPREC_LOGGER_ESD_RECOVERY, 0, 0);
/* C3T code for HQ-219022 by jiangyue at 2022/08/22 start */
#ifdef CONFIG_MI_ERRFLAG_ESD_CHECK_ENABLE
	if(_need_do_esd_check())
	{
		atomic_set(&lcm_ready, 1);
		atomic_set(&lcm_valid_irq, 1);
		DISPERR("[ESD] atomic_set(&lcm_ready, 1)\n");
	}
#endif
/* C3T code for HQ-219022 by jiangyue at 2022/08/22 end */
	return ret;
}

void primary_display_requset_eint(void)
{
	struct LCM_PARAMS *params;
	struct device_node *node;

	params = primary_get_lcm()->params;
	if (params->dsi.customization_esd_check_enable == 0) {
		node = of_find_compatible_node(NULL, NULL,
/* C3T code for HQ-219022 by jiangyue at 2022/08/22 start */
#ifdef CONFIG_MI_ERRFLAG_ESD_CHECK_ENABLE
				"mediatek, dsi_err-flag");
#else
				"mediatek, DSI_TE-eint");
#endif
		if (!node) {
#ifdef CONFIG_MI_ERRFLAG_ESD_CHECK_ENABLE
			DISPERR(
				"[ESD][%s] can't find dsi_err-flag eint compatible node\n",
				    __func__);
#else
			DISPERR(
				"[ESD][%s] can't find DSI_TE-eint eint compatible node\n",
				    __func__);
#endif
/* C3T code for HQ-219022 by jiangyue at 2022/08/22 end */
			return;
		}

		/* 1.register irq handler */
		te_irq = irq_of_parse_and_map(node, 0);
		if (request_irq(te_irq, _esd_check_ext_te_irq_handler,
/* C3T code for HQ-219022 by jiangyue at 2022/08/22 start */
#ifdef CONFIG_MI_ERRFLAG_ESD_CHECK_ENABLE
				IRQF_TRIGGER_FALLING, "dsi_err-flag", NULL)) {
#else
				IRQF_TRIGGER_FALLING, "DSI_TE-eint", NULL)) {
#endif
/* C3T code for HQ-219022 by jiangyue at 2022/08/22 end */
			DISPERR("[ESD]EINT IRQ LINE NOT AVAILABLE!\n");
			return;
		}

		/* 2.disable irq */
		disable_irq(te_irq);

		/* 3.set DSI_TE GPIO to TE MODE */
		disp_dts_gpio_select_state(DTS_GPIO_STATE_TE_MODE_TE);

	}
}

void primary_display_check_recovery_init(void)
{
	/* primary display check thread init */
	primary_display_check_task =
		kthread_create(primary_display_check_recovery_worker_kthread,
			       NULL, "disp_check");
	init_waitqueue_head(&_check_task_wq);

	if (disp_helper_get_option(DISP_OPT_ESD_CHECK_RECOVERY)) {
		wake_up_process(primary_display_check_task);
		if (_need_do_esd_check()) {
			/* esd check init */
			init_waitqueue_head(&esd_ext_te_wq);
			primary_display_requset_eint();
/* C3T code for HQ-219022 by jiangyue at 2022/08/22 start */
#ifndef CONFIG_MI_ERRFLAG_ESD_CHECK_ENABLE
			set_esd_check_mode(GPIO_EINT_MODE);
#endif
/* C3T code for HQ-219022 by jiangyue at 2022/08/22 end */
			primary_display_esd_check_enable(1);
		} else {
			atomic_set(&_check_task_wakeup, 1);
			wake_up_interruptible(&_check_task_wq);
		}
	}
}

void primary_display_esd_check_enable(int enable)
{
	DISPERR("[ESD]%s\n", __func__);
	if (_need_do_esd_check()) {
		if (enable) {
			esd_check_enable = 1;
			DISPCHECK("[ESD]enable esd check\n");
			atomic_set(&_check_task_wakeup, 1);
			wake_up_interruptible(&_check_task_wq);
		} else {
			esd_check_enable = 0;
			atomic_set(&_check_task_wakeup, 0);
			DISPCHECK("[ESD]disable esd check\n");
		}
	} else {
		DISPCHECK("[ESD]do not support esd check\n");
	}
}

unsigned int need_wait_esd_eof(void)
{
	int ret = 1;

	/*
	 * 1.esd check disable
	 * 2.vdo mode
	 * 3.cmd mode te
	 */
	if (_need_do_esd_check() == 0)
		ret = 0;

	if (primary_display_is_video_mode())
		ret = 0;

	if (primary_get_lcm()->params->dsi.customization_esd_check_enable == 0)
		ret = 0;

	return ret;
}

#if defined(CONFIG_MTK_DUAL_DISPLAY_SUPPORT) && \
	(CONFIG_MTK_DUAL_DISPLAY_SUPPORT == 2)
/*********external display dual LCM feature************
 **********esd check*******************************
 */
static unsigned int extd_need_do_esd_check(void)
{
	int ret = 0;
	struct disp_lcm_handle *plcm = NULL;

	extd_disp_get_interface((struct disp_lcm_handle **)&plcm);

	if (plcm && plcm->params->dsi.esd_check_enable == 1)
		ret = 1;

	return ret;
}

/* For external display EXT TE EINT Check */
static irqreturn_t extd_esd_check_ext_te_irq_handler(int irq, void *data)
{
	mmprofile_log_ex(ddp_mmp_get_events()->esd_vdo_eint,
		MMPROFILE_FLAG_PULSE, 0, 0);
	atomic_set(&esd_ext_te_1_event, 1);
	wake_up_interruptible(&esd_ext_te_1_wq);
	return IRQ_HANDLED;
}

static int extd_esd_check_eint(void)
{
	int ret = 0;

	if (wait_event_interruptible_timeout(esd_ext_te_1_wq,
		atomic_read(&esd_ext_te_1_event), HZ / 2) > 0)
		ret = 0;	/* esd check pass */
	else
		ret = 1;	/* esd check fail */

	atomic_set(&esd_ext_te_1_event, 0);

	return ret;
}

static unsigned int get_extd_esd_check_mode(void)
{
	return extd_esd_check_mode;
}

static void set_extd_esd_check_mode(unsigned int mode)
{
	extd_esd_check_mode = mode;
}

void external_display_esd_check_enable(int enable)
{
	if (extd_need_do_esd_check()) {

		if (enable) {
			extd_esd_check_enable = 1;
			DISPCHECK("[EXTD-ESD]enable esd check\n");
			atomic_set(&extd_check_task_wakeup, 1);
			wake_up_interruptible(&extd_check_task_wq);
		} else {
			extd_esd_check_enable = 0;
			atomic_set(&extd_check_task_wakeup, 0);
			DISPCHECK("[EXTD-ESD]disable esd check\n");

		}
	} else
		DISPCHECK("[EXTD-ESD]do not support esd check\n");

}

int external_display_switch_esd_mode(int mode)
{
	int ret = 0;
	struct device_node *node;
	int irq;
	u32 ints[2] = { 0, 0 };

	node = of_find_compatible_node(NULL, NULL, "mediatek, dsi_te_1-eint");
	if (node == NULL) {
		DISPERR(
			"[EXTD-ESD][%s] can't find DSI_TE eint compatible node\n",
			__func__);
		return ret;
	}

	if (mode == GPIO_EINT_MODE) {
		/* register irq handler */
		of_property_read_u32_array(node, "debounce",
			ints, ARRAY_SIZE(ints));
		/* mt_gpio_set_debounce(ints[0], ints[1]); */
		irq = irq_of_parse_and_map(node, 0);
		if (request_irq(irq, extd_esd_check_ext_te_irq_handler,
			IRQF_TRIGGER_RISING, "dsi_te_1-eint", NULL))
			DISPERR("[EXTD-ESD]EINT IRQ LINE NOT AVAILABLE!!\n");
	} else if (mode == GPIO_DSI_MODE) {
		/* 1. unregister irq handler */
		irq = irq_of_parse_and_map(node, 0);
		free_irq(irq, NULL);
		/*disp_dts_gpio_select_state(DTS_GPIO_STATE_TE_MODE_TE);*/
	}

	return ret;
}

/* ESD CHECK FUNCTION */
/* return 1: esd check fail */
/* return 0: esd check pass */
int external_display_esd_check(void)
{
	int ret = 0;
	unsigned int mode;
	struct disp_lcm_handle *plcm = NULL;

	dprec_logger_start(DPREC_LOGGER_ESD_CHECK, 0, 0);
	mmprofile_log_ex(ddp_mmp_get_events()->esd_check_t,
		MMPROFILE_FLAG_START, 0, 0);
	DISPCHECK("[EXTD-ESD]ESD check begin\n");

	if (ext_disp_is_alive() != EXTD_RESUME) {
		mmprofile_log_ex(ddp_mmp_get_events()->esd_check_t,
			MMPROFILE_FLAG_PULSE, 1, 0);
		DISPCHECK("[EXTD-ESD]EXTD DISP is slept. skip esd check\n");
		goto done;
	}

	/*  Esd Check : EXT TE */
	extd_disp_get_interface((struct disp_lcm_handle **)&plcm);
	if (!plcm || plcm->params->dsi.customization_esd_check_enable != 0)
		goto done;
	/* use te for esd check */
	mmprofile_log_ex(ddp_mmp_get_events()->esd_extte,
		MMPROFILE_FLAG_START, 0, 0);

	mode = get_extd_esd_check_mode();
	if (mode == GPIO_EINT_MODE) {
		DISPCHECK("[EXTD-ESD]ESD check eint\n");
		mmprofile_log_ex(ddp_mmp_get_events()->esd_extte,
			MMPROFILE_FLAG_PULSE, ext_disp_is_video_mode(), mode);
		external_display_switch_esd_mode(mode);
		DISPCHECK("[EXTD-ESD]ESD check begin ~\n");
		ret = extd_esd_check_eint();
		DISPCHECK("[EXTD-ESD]ESD check end, ret:%d\n", ret);
		mode = GPIO_DSI_MODE; /* used for mode switch */
		external_display_switch_esd_mode(mode);
	} else if (mode == GPIO_DSI_MODE) {
		mmprofile_log_ex(ddp_mmp_get_events()->esd_extte,
			MMPROFILE_FLAG_PULSE, ext_disp_is_video_mode(), mode);

		DISPCHECK("[EXTD-ESD]ESD check read\n");
		/*ret = do_esd_check_read();*/
		mode = GPIO_EINT_MODE; /* used for mode switch */
	}

	mmprofile_log_ex(ddp_mmp_get_events()->esd_extte,
		MMPROFILE_FLAG_END, 0, ret);

done:
	DISPCHECK("[EXTD-ESD]ESD check end, ret = %d\n", ret);
	mmprofile_log_ex(ddp_mmp_get_events()->esd_check_t,
		MMPROFILE_FLAG_END, 0, ret);
	dprec_logger_done(DPREC_LOGGER_ESD_CHECK, 0, 0);
	return ret;

}

static int external_display_check_recovery_worker_kthread(void *data)
{
	struct sched_param param = {.sched_priority = 87 };
	int ret = 0;
	int i = 0;
	int esd_try_cnt = 5;	/* 20; */
	int recovery_done = 0;

	DISPFUNC();
	sched_setscheduler(current, SCHED_RR, &param);

	while (1) {
		msleep(2000);/*2s*/
		ret = wait_event_interruptible(extd_check_task_wq,
			atomic_read(&extd_check_task_wakeup));
		if (ret < 0) {
			DISPCHECK(
				"[ext_disp_check]check thread waked up accidently\n");
			continue;
		}
		pr_debug("[EXTD ext_disp_check]check thread waked up!\n");
		ext_disp_esd_check_lock();
		/* esd check and  recovery */

		if (!extd_esd_check_enable) {
			ext_disp_esd_check_unlock();
			continue;
		}

		i = 0;/*repeat*/
		do {
			ret = external_display_esd_check();
			if (ret != 1)
				break;
			DISPERR(
				"[EXTD-ESD]esd check fail, will do esd recovery. try=%d\n",
				i);
			ext_disp_esd_recovery();
			recovery_done = 1;

		} while (++i < esd_try_cnt);

		if (ret == 1) {
			DISPERR(
				"[EXTD-ESD]after esd recovery %d times, still fail, disable esd check\n",
				esd_try_cnt);
			external_display_esd_check_enable(0);
		} else if (recovery_done == 1) {
			DISPCHECK("[EXTD-ESD]esd recovery success\n");
			recovery_done = 0;
		}
		ext_disp_esd_check_unlock();

		if (kthread_should_stop())
			break;
	}

	return 0;
}

void external_display_check_recovery_init(void)
{
	/* primary display check thread init */
	if (external_display_check_task == NULL) {
		external_display_check_task =
			kthread_create(
			external_display_check_recovery_worker_kthread,
			NULL, "extd_esd_check");
		init_waitqueue_head(&extd_check_task_wq);
		wake_up_process(external_display_check_task);
	}

	if (disp_helper_get_option(DISP_OPT_ESD_CHECK_RECOVERY)
		&& extd_need_do_esd_check()) {
		/* esd check init */
		init_waitqueue_head(&esd_ext_te_1_wq);
		set_extd_esd_check_mode(GPIO_EINT_MODE);
		/*external_display_esd_check_enable(1);*/
	}

}
#endif

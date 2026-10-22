/*
 * SPDX-FileCopyrightText: 2019-2025 SiFli Technologies(Nanjing) Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "rtthread.h"
#include "bts2_app_inc.h"
#include "ipc/ringbuffer.h"
#ifdef CFG_AV_SRC

#define LOG_TAG         "audio_av_src"
//#define DBG_LVL          LOG_LVL_INFO
#include "log.h"

#define MAX_FRM_PER_PAYLOAD 10

//#define AUDIO_DATA_TEST
//#define AUDIO_DATA_FROM_PSRAM 1
#ifdef AUDIO_DATA_TEST

    #if AUDIO_DATA_FROM_PSRAM
        #define INPUT_ADDRESS  0x60100000
        #define INPUT_END_ADDRESS  0x60400000
        static uint8_t *datain, *dataend;
    #endif

    static FILE *stream;
#endif // AUDIO_DATA_TEST

#ifdef AUDIO_USING_MANAGER
    #include "audio_server.h"
#endif // AUDIO_USING_MANAGER


#if defined(AUDIO_DATA_TEST)
    static void bt_avsrc_send_data(U16 m, void *bts2_app_data);
#elif defined(AUDIO_USING_MANAGER)
    #define A2DP_CMD_PAUSE          0
    #define A2DP_CMD_RESUME         1

    static uint16_t bt_avsrc_send_data(struct rt_ringbuffer32 *rb);
    static int bt_avsrc_register_audio_open(void *user_data, audio_device_input_callback callback);
    static int bt_avsrc_register_audio_close(void *user_data);
    static uint32_t bt_avsrc_register_audio_output(void *user_data, struct rt_ringbuffer32 *rb);
    static int bt_avsrc_register_audio_ioctl(void *user_data, int cmd, void *val);
#endif


#ifdef AUDIO_USING_MANAGER
static const struct audio_device g_bt_avsrc_audio_dev =
{
    .open = bt_avsrc_register_audio_open,
    .close = bt_avsrc_register_audio_close,
    .user_data = NULL,
    .output = bt_avsrc_register_audio_output,
    .ioctl = bt_avsrc_register_audio_ioctl,
};


static int bt_avsrc_register_audio_open(void *user_data, int (*input)(audio_server_callback_cmt_t cmd, const uint8_t *buffer, uint32_t size))
{
    bts2s_av_inst_data *inst = bt_av_get_inst_data();
    inst->src_data.input_cb = input;
    inst->src_data.audio_state = AVSRC_AUDIO_SER_OPEN;
    if (get_server_current_play_status())
    {
        for (uint32_t i = 0; i < MAX_CONNS; i++)
        {
            if (inst->con[i].cfg == AV_AUDIO_SRC)
                bt_av_start_stream_by_audio_server(i);
        }
    }

    return 0;
}

//zhengyu:avoid timer timeout after audio close
static U8 send_timer_added = 0;
static int bt_avsrc_register_audio_close(void *user_data)
{
    bts2s_av_inst_data *inst = bt_av_get_inst_data();
    if (send_timer_added == 1)
    {
        bts2_timer_ev_cancel(inst->src_data.tid, NULL, NULL);
        inst->src_data.tid = 0;
        send_timer_added = 0;
    }
    bt_avsrc_sharing_cancel();
    inst->src_data.input_cb = NULL;
    inst->src_data.audio_state = AVSRC_AUDIO_SER_IDLE;
    for (uint32_t i = 0; i < MAX_CONNS; i++)
    {
        if (inst->con[i].cfg == AV_AUDIO_SRC)
            bt_av_suspend_stream_by_audio_server(i);
    }
    return 0;
}

static uint32_t bt_avsrc_register_audio_output(void *user_data, struct rt_ringbuffer32 *rb)
{
    uint16_t rd_len = bt_avsrc_send_data(rb);
    return rd_len;
}


static int bt_avsrc_register_audio_ioctl(void *user_data, int cmd, void *val)
{
    bts2s_av_inst_data *inst = bt_av_get_inst_data();
    if (cmd == A2DP_CMD_PAUSE)
    {
        if (send_timer_added == 1)
        {
            bts2_timer_ev_cancel(inst->src_data.tid, NULL, NULL);
            inst->src_data.tid = 0;
            send_timer_added = 0;
        }
        bt_avsrc_sharing_cancel();
        inst->src_data.input_cb = NULL;
        inst->src_data.audio_state = AVSRC_AUDIO_SER_IDLE;
        for (uint32_t i = 0; i < MAX_CONNS; i++)
            bt_av_suspend_stream_by_audio_server(i);
    }
    else if (cmd == A2DP_CMD_RESUME)
    {
        inst->src_data.audio_state = AVSRC_AUDIO_SER_OPEN;
        for (uint32_t i = 0; i < MAX_CONNS; i++)
            bt_av_start_stream_by_audio_server(i);
    }
    return 0;
}


static int bt_avsrc_register_audio_service(bts2_app_stru *bts2_app_data)
{
    return audio_server_register_audio_device(AUDIO_DEVICE_A2DP_SINK, (struct audio_device *)&g_bt_avsrc_audio_dev);
}



static void bt_avsrc_send_cb(uint16_t m, void *para)
{
    struct rt_ringbuffer32 *rb = (struct rt_ringbuffer32 *)para;
    send_timer_added = 0;
    bt_avsrc_send_data(rb);
}
#endif // AUDIO_USING_MANAGER

/*----------------------------------------------------------------------------*
 *
 * DESCRIPTION:
 *
 * INPUT:
 *      bts2_app_stru *bts2_app_data:
 *
 * OUTPUT:
 *      void.
 *
 * NOTE:
 *      none.
 *
 *----------------------------------------------------------------------------*/
int bt_a2dp_sink_connect_request(BTS2S_BD_ADDR *bd)
{
    bt_av_conn(bd, AV_SNK);
    return 0;
}

/*----------------------------------------------------------------------------*
 *
 * DESCRIPTION:
 *
 * INPUT:
 *      bts2_app_stru *bts2_app_data:
 *
 * OUTPUT:
 *      void.
 *
 * NOTE:
 *      none.
 *
 *----------------------------------------------------------------------------*/
void bt_avsrc_get_cfg(bts2_app_stru *bts2_app_data)
{
    for (uint32_t i = 0; i < MAX_CONNS; i++)
        bt_av_get_cfg(i);
}

/*----------------------------------------------------------------------------*
 *
 * DESCRIPTION:
 *
 * INPUT:
 *      bts2_app_stru *bts2_app_data:
 *
 * OUTPUT:
 *      void.
 *
 * NOTE:
 *      none.
 *
 *----------------------------------------------------------------------------*/
void bt_avsrc_recfg(bts2_app_stru *bts2_app_data)
{
// TODO: recfg
#if 0
    bts2s_avsrc_inst_data *inst;
    U8 app_serv_cap[SBC_MEDIA_CODEC_SC_SIZE];
    U8 i;
    inst = global_inst;
#ifdef TP_SIG_SMG_BI_14_C_1
    if (inst->con[inst->con_idx].st == avconned_open)
    {
        /*  case1  invalid category */
        app_serv_cap[0] = 0xFF;
        app_serv_cap[1] = 0xFF;
        app_serv_cap[2] = 0xFF;
        app_serv_cap[3] = 0xFF;
        app_serv_cap[4] = 0xFF;
        app_serv_cap[5] = 0xFF;
        app_serv_cap[6] = 0xFF;
        app_serv_cap[7] = 0xFF;
        av_recfg_req(inst->con[inst->con_idx].stream_hdl, ASSIGN_TLABEL, SBC_MEDIA_CODEC_SC_SIZE, app_serv_cap);
    }
#elif defined(TP_SIG_SMG_BI_14_C_3)
    if (inst->con[inst->con_idx].st == avconned_open)
    {
        /*  case3  valid but not support service category */
        app_serv_cap[0] = AV_SC_MEDIA_CODEC;
        app_serv_cap[1] = SBC_MEDIA_CODEC_SC_SIZE - 2;
        app_serv_cap[2] = AV_AUDIO << 4;
        app_serv_cap[3] = AV_MPEG12_AUDIO;/*mp3*/
        app_serv_cap[4] = 0x32;
        app_serv_cap[5] = 0x41;
        app_serv_cap[6] = 0x89;
        app_serv_cap[7] = 0x32;
        av_recfg_req(inst->con[inst->con_idx].stream_hdl, ASSIGN_TLABEL, SBC_MEDIA_CODEC_SC_SIZE, app_serv_cap);
    }
#else
    if (inst->con[inst->con_idx].st == avconned_open)
    {
        app_serv_cap[0] = AV_SC_MEDIA_CODEC;
        app_serv_cap[1] = SBC_MEDIA_CODEC_SC_SIZE - 2;
        app_serv_cap[2] = AV_AUDIO << 4;
        app_serv_cap[3] = AV_SBC;
        for (i = 0; i < SBC_MEDIA_CODEC_SC_SIZE - 4; i++)
        {
            app_serv_cap[4 + i] = sbc_cfg[1][i];/*choose the other sbc code mothod*/
        }
        bt_avsrc_store_sbc_cfg(&inst->con[inst->con_idx].act_cfg, app_serv_cap + 4);
        INFO_TRACE(">> reset the configuration of the stream\n");
        av_recfg_req(inst->con[inst->con_idx].stream_hdl, ASSIGN_TLABEL, SBC_MEDIA_CODEC_SC_SIZE, app_serv_cap);
    }
    else
    {
        if (inst->con[inst->con_idx].st == avconned_streaming)
        {
            INFO_TRACE(" -- the stream is streaming, please supped it!\n");
        }
        else
        {
            INFO_TRACE(" -- the stream is not in use!\n");
        }
    }
#endif
#endif
}
/*----------------------------------------------------------------------------*
 *
 * DESCRIPTION:
 *
 * INPUT:
 *      bts2_app_stru *bts2_app_data:
 *
 * OUTPUT:
 *      void.
 *
 * NOTE:
 *      none.
 *
 *----------------------------------------------------------------------------*/
void bt_avsrc_disc_2_snk(void)
{
    bts2s_av_inst_data *inst_data;
    inst_data = bt_av_get_inst_data();

    for (uint32_t i = 0; i < MAX_CONNS; i++)
    {
        if (inst_data->con[i].cfg == AV_AUDIO_SRC)
            bt_av_disconnect(i);
    }
}

void bt_avsrc_close_a2dp(void)
{
    bts2s_av_inst_data *inst_data;
    inst_data = bt_av_get_inst_data();

    inst_data->close_pending = TRUE;
    for (uint32_t i = 0; i < MAX_CONNS; i++)
    {
        if (inst_data->con[i].cfg == AV_AUDIO_SRC)
            bt_av_disconnect(i);
    }
}

void bt_avsrc_disc_by_addr(BTS2S_BD_ADDR *bd_addr)
{
    bts2s_av_inst_data *inst_data;
    inst_data = bt_av_get_inst_data();

    for (uint32_t i = 0; i < MAX_CONNS; i++)
    {
        if (bd_eq(bd_addr, &inst_data->con[i].av_rmt_addr))
        {
            bt_av_disconnect(i);
            break;
        }
    }
}

/*----------------------------------------------------------------------------*
 *
 * DESCRIPTION:
 *
 * INPUT:
 *      bts2_app_stru *bts2_app_data:
 *
 * OUTPUT:
 *      void.
 *
 * NOTE:
 *      none.
 *
 *----------------------------------------------------------------------------*/
void bt_avsrc_suspend_stream(bts2_app_stru *bts2_app_data)
{
    for (uint32_t i = 0; i < MAX_CONNS; i++)
        bt_av_suspend_stream(i);

}

/*----------------------------------------------------------------------------*
 *
 * DESCRIPTION:
 *
 * INPUT:
 *      bts2_app_stru *bts2_app_data:
 *
 * OUTPUT:
 *      void.
 *
 * NOTE:
 *      none.
 *
 *----------------------------------------------------------------------------*/
void bt_avsrc_abort_stream(bts2_app_stru *bts2_app_data)
{
    for (uint32_t i = 0; i < MAX_CONNS; i++)
        bt_av_abort_stream(i);
}

/*----------------------------------------------------------------------------*
 *
 * DESCRIPTION:
 *
 * INPUT:
 *      bts2_app_stru *bts2_app_data:
 *
 * OUTPUT:
 *      void.
 *
 * NOTE:
 *      none.
 *
 *----------------------------------------------------------------------------*/
void bt_avsrc_start_stream(bts2_app_stru *bts2_app_data)
{
    for (uint32_t i = 0; i < MAX_CONNS; i++)
        bt_av_start_stream(i);

}

/*----------------------------------------------------------------------------*
 *
 * DESCRIPTION:
 *
 * INPUT:
 *      bts2_app_stru *bts2_app_data:
 *
 * OUTPUT:
 *      void.
 *
 * NOTE:
 *      none.
 *
 *----------------------------------------------------------------------------*/
void bt_avsrc_release_stream(bts2_app_stru *bts2_app_data)
{
    for (uint32_t i = 0; i < MAX_CONNS; i++)
        bt_av_release_stream(i);

}

/*----------------------------------------------------------------------------*
 *
 * DESCRIPTION:
 *
 * INPUT:
 *      bts2_app_stru *bts2_app_data:
 *
 * OUTPUT:
 *      void.
 *
 * NOTE:
 *      none.
 *
 *----------------------------------------------------------------------------*/
int bt_avsrc_get_plyback_conn(bts2s_av_inst_data *inst)
{
    U8 found = FALSE;
    U8 i = 0;
    int res = - 1;

    if (inst == NULL)
    {
        return res;
    }

    for (i = 0; i < MAX_CONNS; i++)
    {
        if ((inst->con[i].local_seid_idx != 0xff) && (inst->local_seid_info[inst->con[i].local_seid_idx].local_seid.sep_type == AV_SRC) &&
                (inst->local_seid_info[inst->con[i].local_seid_idx].local_seid.in_use == TRUE))
        {
            found = TRUE;
            break;
        }
    }

    if (found)
    {
        res = i;
    }

    return res;
}

int bt_avsrc_get_stream_conn(bts2s_av_inst_data *inst)
{
    for (U8 con_idx = 0; con_idx < MAX_CONNS; con_idx++)
    {
        if ((inst->con[con_idx].st == avconned_streaming) && (inst->con[con_idx].cfg == AV_AUDIO_SRC))
        {
            return con_idx;
        }
    }

    return -1;
}

/* Per-connection media forwarding diagnostics for the shared sender.
 * req   : payloads handed to av_stream_data_req() for this stream
 * nomem : fan-out copies dropped because bmalloc() failed
 * qos   : AV QOS indications seen for this stream (the AVDTP layer only
 *         emits these while media actually moves through the stream)
 * buff  : last reported stream buffer fullness (0 empty .. 10 full)
 * A streaming connection whose req/qos counters stall while another
 * connection advances has a wedged media path inside the AVDTP layer. */
static U32 s_media_req[MAX_CONNS];
static U32 s_media_nomem[MAX_CONNS];
static U32 s_media_qos[MAX_CONNS];
static U8 s_media_buff[MAX_CONNS];

void bt_avsrc_note_qos(U16 shdl, U16 buff_st)
{
    bts2s_av_inst_data *inst = bt_av_get_inst_data();
    U8 idx;

    if (inst == NULL)
    {
        return;
    }
    idx = bt_av_get_idx_from_shdl(inst, (U8)shdl);
    if (idx < MAX_CONNS)
    {
        s_media_qos[idx]++;
        s_media_buff[idx] = (U8)((buff_st > 10U) ? 10U : buff_st);
    }
}

BOOL bt_avsrc_get_media_stats(U8 idx, U32 *req, U32 *nomem, U32 *qos, U8 *buff)
{
    if (idx >= MAX_CONNS)
    {
        return FALSE;
    }
    if (req != NULL)
    {
        *req = s_media_req[idx];
    }
    if (nomem != NULL)
    {
        *nomem = s_media_nomem[idx];
    }
    if (qos != NULL)
    {
        *qos = s_media_qos[idx];
    }
    if (buff != NULL)
    {
        *buff = s_media_buff[idx];
    }
    return TRUE;
}

/* Serializes the shared SBC encoder state (bts2_sbc_encode/cfg/completed).
 * The encoder library keeps global state and is not reentrant: the media
 * producer thread encodes while the BT task may reconfigure the encoder
 * when a stream opens or closes. A mutex instead of rt_enter_critical keeps
 * the scheduler live during multi-millisecond encode bursts so the I2S RX
 * thread can still drain the driver's record pipe (a scheduler lock there
 * let the pipe overflow and spliced dropped PCM into the SBC stream). */
static struct rt_mutex g_sbc_lock;
static uint8_t g_sbc_lock_ready = 0;

void bt_avsrc_sbc_lock(void)
{
    rt_base_t level = rt_hw_interrupt_disable();
    if (!g_sbc_lock_ready)
    {
        rt_mutex_init(&g_sbc_lock, "avsbcl", RT_IPC_FLAG_PRIO);
        g_sbc_lock_ready = 1;
    }
    rt_hw_interrupt_enable(level);
    rt_mutex_take(&g_sbc_lock, RT_WAITING_FOREVER);
}

void bt_avsrc_sbc_unlock(void)
{
    rt_mutex_release(&g_sbc_lock);
}

/* Serializes the media pump against the audio-manager send path: both do
 * read-modify-write on inst->src_data pacing fields (m_sec_time_4_next_pkt
 * and friends) and both call into bmalloc/av_stream_data_req. Same lazy
 * rt_mutex pattern as g_sbc_lock; lock order is tx_lock -> sbc_lock only,
 * so no inversion with bt_avsrc_send_data()'s encoder section. */
static struct rt_mutex g_tx_lock;
static uint8_t g_tx_lock_ready = 0;

static void bt_avsrc_tx_lock(void)
{
    rt_base_t level = rt_hw_interrupt_disable();
    if (!g_tx_lock_ready)
    {
        rt_mutex_init(&g_tx_lock, "avtxl", RT_IPC_FLAG_PRIO);
        g_tx_lock_ready = 1;
    }
    rt_hw_interrupt_enable(level);
    rt_mutex_take(&g_tx_lock, RT_WAITING_FOREVER);
}

static void bt_avsrc_tx_unlock(void)
{
    rt_mutex_release(&g_tx_lock);
}

/* ---- Media send buffer pool --------------------------------------------
 * The per-sink fan-out used to bmalloc() one ~631 B copy per packet and
 * silently drop the packet when the heap was busy (SDP/HFP allocations
 * during the second-headset connect). The dropped record had already
 * advanced inst->time_stamp while the per-stream RTP seq did not, so the
 * sink saw an RTP ts jump and the decoder chain hiccupped (the "seq-556
 * click" class).
 *
 * The stack takes ownership of each copy and returns it through
 * bt_mem_free() (weak trampoline to rt_free; l2cm_datawr_free /
 * avh_stream_data_req / av_stream_buff_clear all free via it), so a plain
 * static pool would make rt_free() receive a static address and corrupt
 * the heap. Instead the pool owns its blocks and only bt_mem_free() is
 * redirected for pool addresses; every other free is byte-identical to the
 * default trampoline. Allocation falls back to bt_mem_alloc() when the
 * pool is empty so worst-case pressure degrades to the old behavior
 * instead of failing hard. Free-list ops are O(1) under an interrupt
 * disable (alloc from "a2dptx", free from the BT task). */
static void *bt_avsrc_media_alloc(rt_size_t size);

#ifndef BSP_USING_PC_SIMULATOR
#define BT_AVSRC_MEM_POOL_BLKS   16
#define BT_AVSRC_MEM_POOL_BYTES  704   /* >= 10 * 63 + 13 SBC media pkt */
ALIGN(4) static uint8_t s_mem_pool[BT_AVSRC_MEM_POOL_BLKS][BT_AVSRC_MEM_POOL_BYTES];
static uint8_t *s_mem_pool_free[BT_AVSRC_MEM_POOL_BLKS];
static uint32_t s_mem_pool_cnt = 0;
static uint8_t s_mem_pool_ready = 0;

void bt_mem_free(void *ptr)
{
    uint8_t *p8 = (uint8_t *)ptr;
    uint8_t *pool = &s_mem_pool[0][0];

    if (p8 == NULL)
        return;

    /* Byte-wise bounds. s_mem_pool is uint8_t[BLKS][BYTES], so a bare
     * s_mem_pool + sizeof(s_mem_pool) pointer add scales by BYTES and the
     * upper bound lands ~704x too far — that version swallowed every heap
     * free into the pool list (the heap sits inside the bogus range),
     * leaked the heap full, and hcit_send_acl_data then NULL-derefed its
     * bt_mem_alloc() result. Keep the end pointer byte-addressed. */
    if ((p8 >= pool) && (p8 < pool + sizeof(s_mem_pool)))
    {
        rt_base_t level = rt_hw_interrupt_disable();
        if (s_mem_pool_cnt < BT_AVSRC_MEM_POOL_BLKS)
            s_mem_pool_free[s_mem_pool_cnt++] = p8;
        rt_hw_interrupt_enable(level);
        return;
    }
    rt_free(p8);
}

static void *bt_avsrc_media_alloc(rt_size_t size)
{
    void *p = NULL;

    if (size <= BT_AVSRC_MEM_POOL_BYTES)
    {
        rt_base_t level = rt_hw_interrupt_disable();
        if (!s_mem_pool_ready)
        {
            /* Blocks can only be handed out after this seed, so a
             * bt_mem_free() of a pool address can never race it. */
            for (uint32_t i = 0; i < BT_AVSRC_MEM_POOL_BLKS; i++)
                s_mem_pool_free[s_mem_pool_cnt++] = s_mem_pool[i];
            s_mem_pool_ready = 1;
        }
        if (s_mem_pool_cnt > 0)
            p = s_mem_pool_free[--s_mem_pool_cnt];
        rt_hw_interrupt_enable(level);
        if (p != NULL)
            return p;
    }
    return bmalloc(size);
}
#else /* BSP_USING_PC_SIMULATOR: no weak trampoline to override */
static void *bt_avsrc_media_alloc(rt_size_t size)
{
    return bmalloc(size);
}
#endif /* BSP_USING_PC_SIMULATOR */

/* ---- Dedicated A2DP media TX thread ("a2dptx") --------------------------
 * The send pump used to be a bts2 timer callback on the "bts" task.
 * bts2_task_run drains all message queues before expired timers, so any
 * signaling storm (HFP SLC while the second headset connects) starves the
 * ~29 ms send timer for hundreds of ms and the forwarded audio stutters.
 * av_stream_data_req itself is only bt_mem_alloc + bts2_msg_put (thread
 * safe; its message path stayed alive throughout the observed 360 ms gap),
 * so pacing now runs on a dedicated RT-Thread above the BT task and the
 * prebuilt stack needs no change. "a2dptx" is the only caller of
 * bt_avsrc_sharing() and therefore the only reader of the share ring; the
 * app kick arms this thread via bt_avsrc_sharing_sched() instead of
 * calling bt_avsrc_sharing() itself. */
#define BT_AVSRC_TX_EVT_SCHED   (1U << 0)
#define BT_AVSRC_TX_EVT_STOP    (1U << 1)
/* Catch-up step after a stall: bounded spacing keeps the RTP cadence even
 * while still refilling the sink's jitter buffer quickly. 2 ms refills a
 * 4-deep buffer in ~16 ms (was 40 ms at 5 ms steps) so a transient stall
 * no longer exceeds the sink's 225 ms jitter buffer. */
#define BT_AVSRC_TX_CATCHUP_MS  2
#define BT_AVSRC_TX_STACK_SIZE  2048
/* Credit gate: av_get_stream_buffize() reads a single global that the
 * prebuilt stack sets to the media FIFO depth of whichever stream last
 * queued or dequeued a packet — NOT the combined depth across streams.
 * With one sink it is accurate; with two sinks it flaps between the two
 * per-stream depths and undercounts the real pipeline occupancy. The
 * threshold is therefore tighter (2) as a single-stream guard, and a
 * combined-pipeline counter below covers the dual-stream case. */
#define BT_AVSRC_TX_CREDIT_HOLD 2
/* Combined pipeline budget: the controller has 4 ACL buffers shared
 * between media and signaling (the Frontline "credits: -4" window). The
 * gate keeps total media copies in the stack/controller pipeline <= 3,
 * leaving one slot for AVDTP/HFP signaling — shortening the connect
 * storm that causes both 丢包 and the connect-window stutter. */
#define BT_AVSRC_TX_PIPE_MAX    3
/* Conservative per-copy drain time through L2CA -> HCI -> controller.
 * At 2-DH5 one ACL slot frees roughly every 1-2 ms; 2 ms is the safe
 * estimate so the combined counter never undercounts the pipeline. */
#define BT_AVSRC_TX_DRAIN_MS    2
/* Poll period while a record is held (credit gate / alloc retry): short
 * enough that releasing is felt within one audio frame slice, long enough
 * not to spin. */
#define BT_AVSRC_TX_HOLD_MS     1
/* Consecutive fan-out allocation failures tolerated before the pending
 * record is dropped (WITHOUT advancing time_stamp) to keep the chain
 * alive. 32 polls x 1 ms = 32 ms of retrying covers every transient heap
 * squeeze seen in captures; beyond that a frozen stream would be worse
 * than one lost frame. */
#define BT_AVSRC_TX_FAILS_MAX   32

#ifdef CFG_AV_SHARING
static struct rt_event s_avsrc_tx_evt;
static volatile uint8_t s_tx_ready = 0;
static volatile uint8_t s_tx_armed = 0;
static volatile uint8_t s_tx_stop_req = 0;
static volatile int32_t s_tx_delay_ms = 0;
static struct rt_ringbuffer32 *s_tx_rb = NULL;
/* Combined pipeline tracker: counts every media copy handed to
 * av_stream_data_req (fan-out included) and decays on a conservative
 * per-copy drain estimate. This is the only reliable multi-stream
 * signal — see BT_AVSRC_TX_PIPE_MAX above. */
static U16 s_tx_pipe_copies = 0;
static U32 s_tx_pipe_tick_ms = 0;

static void bt_avsrc_pipe_decay(void)
{
    U32 now = BTS2_GET_TIME();
    U32 elapsed = now - s_tx_pipe_tick_ms;
    U16 drained = (U16)(elapsed / BT_AVSRC_TX_DRAIN_MS);
    if (drained >= s_tx_pipe_copies)
        s_tx_pipe_copies = 0;
    else
        s_tx_pipe_copies -= drained;
    s_tx_pipe_tick_ms = now;
}

int bt_avsrc_sharing_armed(void)
{
    return s_tx_armed ? 1 : 0;
}

void bt_avsrc_sharing_cancel(void)
{
    /* Sticky until the next explicit sched(): a fire already in flight
     * must not rearm itself through the end-of-pump reschedule. */
    s_tx_stop_req = 1;
    s_tx_armed = 0;
    /* Pipeline is going away: any copies we counted as in-flight will
     * never see data_cfm, so reset the combined counter now. */
    s_tx_pipe_copies = 0;
    if (s_tx_ready)
        rt_event_send(&s_avsrc_tx_evt, BT_AVSRC_TX_EVT_STOP);
}

static struct rt_thread s_avsrc_tx_thread;
ALIGN(4) static rt_uint8_t s_avsrc_tx_stack[BT_AVSRC_TX_STACK_SIZE];

static void bt_avsrc_tx_arm(struct rt_ringbuffer32 *rb, int32_t delay_ms)
{
    if (delay_ms < 1)
        delay_ms = 1;
    s_tx_rb = rb;
    s_tx_delay_ms = delay_ms;
    s_tx_armed = 1;
    rt_event_send(&s_avsrc_tx_evt, BT_AVSRC_TX_EVT_SCHED);
}

/* End-of-pump reschedule: honors a concurrent cancel. */
static void bt_avsrc_sharing_rearm(struct rt_ringbuffer32 *rb, int32_t delay_ms)
{
    if (!s_tx_stop_req)
        bt_avsrc_tx_arm(rb, delay_ms);
}

static void bt_avsrc_tx_thread(void *param)
{
    (void)param;

    while (1)
    {
        rt_uint32_t recvd = 0;
        rt_err_t err;
        int32_t delay;

        if (!s_tx_armed)
        {
            rt_event_recv(&s_avsrc_tx_evt,
                          BT_AVSRC_TX_EVT_SCHED | BT_AVSRC_TX_EVT_STOP,
                          RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                          RT_WAITING_FOREVER, &recvd);
            if (recvd & BT_AVSRC_TX_EVT_STOP)
                s_tx_armed = 0;
            continue;
        }

        delay = s_tx_delay_ms;
        err = rt_event_recv(&s_avsrc_tx_evt,
                            BT_AVSRC_TX_EVT_SCHED | BT_AVSRC_TX_EVT_STOP,
                            RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                            delay > 0 ? rt_tick_from_millisecond(delay) : 0,
                            &recvd);
        if (recvd & BT_AVSRC_TX_EVT_STOP)
        {
            s_tx_armed = 0;
            continue;
        }
        if (err == RT_EOK)
            continue;               /* re-armed: pick up the new delay */

        /* Deadline reached: one pump iteration. s_tx_armed is cleared
         * before the body so an early return (no conn / suspend gate)
         * leaves the chain idle and the app pump can re-kick it. */
        s_tx_armed = 0;
        bt_avsrc_sharing(s_tx_rb);
    }
}

static void bt_avsrc_tx_init_once(void)
{
    /* Interrupt-disable guard (not g_tx_lock): sched() may be called while
     * the producer holds the SBC lock, and bt_avsrc_send_data() takes
     * tx_lock -> sbc_lock, so init must never nest the other way. */
    rt_base_t level = rt_hw_interrupt_disable();
    if (!s_tx_ready)
    {
        rt_event_init(&s_avsrc_tx_evt, "avtxe", RT_IPC_FLAG_PRIO);
        rt_thread_init(&s_avsrc_tx_thread, "a2dptx", bt_avsrc_tx_thread, RT_NULL,
                       s_avsrc_tx_stack, sizeof(s_avsrc_tx_stack),
                       RT_THREAD_PRIORITY_HIGH - 1, RT_THREAD_TICK_DEFAULT);
        rt_thread_startup(&s_avsrc_tx_thread);
        s_tx_ready = 1;
    }
    rt_hw_interrupt_enable(level);
}

void bt_avsrc_sharing_sched(struct rt_ringbuffer32 *rb, int32_t delay_ms)
{
    bt_avsrc_tx_init_once();
    s_tx_stop_req = 0;
    bt_avsrc_tx_arm(rb, delay_ms);
}
#endif /* CFG_AV_SHARING */
/*----------------------------------------------------------------------------*
 *
 * DESCRIPTION:
 *
 * INPUT:
 *      bts2_app_stru *bts2_app_data:
 *
 * OUTPUT:
 *      void.
 *
 * NOTE:
 *      none.
 *
 *----------------------------------------------------------------------------*/
static void bt_avsrc_init_data(bts2s_av_inst_data *inst, bts2_app_stru *bts2_app_data)
{
    /*for IOPT test*/
#ifdef AUDIO_USING_MANAGER
    inst->src_data.audio_state = AVSRC_AUDIO_SER_IDLE;
    inst->src_data.tid = 0;
    inst->src_data.stream_frm_time_begin = 0;
#endif // AUDIO_USING_MANAGER

#ifdef CFG_AV_SHARING
    inst->src_data.discard_data = NULL; //bt_avsrc_discard_data;
#endif
}

/*----------------------------------------------------------------------------*
 *
 * DESCRIPTION:
 *
 * INPUT:
 *      bts2_app_stru *bts2_app_data:
 *
 * OUTPUT:
 *      void.
 *
 * NOTE:
 *      none.
 *
 *----------------------------------------------------------------------------*/
static U8 bt_avsrc_role_type_equal(U16 r1, U8 r2)
{
    U8 res;

    if ((r1 == AV_AUDIO_SRC) && (r2 == AV_SRC))
    {
        res = TRUE;
    }
    else if ((r1 == AV_AUDIO_SNK) && (r2 == AV_SNK))
    {
        res = TRUE;
    }
    else
    {
        res = FALSE;
    }

    return res;
}

/*----------------------------------------------------------------------------*
 *
 * DESCRIPTION:
 *
 * INPUT:
 *      bts2_app_stru *bts2_app_data:
 *
 * OUTPUT:
 *      void.
 *
 * NOTE:
 *      none.
 *
 *----------------------------------------------------------------------------*/
extern U8 BQB_TEST_CASE;
U8 bt_avsrc_prepare_sbc(bts2s_av_inst_data *inst, U8 con_idx)
{
    U8 bit_pool_alt;
    U8 toggle_period;
    bts2_sbc_cfg *cfg = &inst->con[con_idx].act_cfg;
    cfg->bit_pool = bts2_sbc_calc_bit_pool(&bit_pool_alt,
                                           &toggle_period,
                                           cfg->chnl_mode,
                                           cfg->sample_freq,
                                           cfg->blocks,
                                           cfg->subbands,
                                           BIT_RATE_DEAFLUT);

    if (cfg->bit_pool > cfg->max_bitpool)
    {
        cfg->bit_pool = cfg->max_bitpool;
    }

    LOG_D("chnl_mode = %d,alloc_method = %d,sample_freq = %d\n", cfg->chnl_mode,
          cfg->alloc_method,
          cfg->sample_freq);
    LOG_D("blocks = %d,subbands = %d,bit_pool = %d\n", cfg->blocks,
          cfg->subbands,
          cfg->bit_pool);

#ifdef BSP_BQB_TEST
    switch (BQB_TEST_CASE)
    {
    case A2DP_SRC_CC_BV_09_I:
        cfg->chnl_mode = SBC_JOINT_STEREO;
        cfg->sample_freq = 44100;
        cfg->blocks = 16;
        cfg->subbands = 8;
        cfg->bit_pool = 53;
        cfg->alloc_method = SBC_METHOD_LOUDNESS;
        break;
    case A2DP_SRC_CC_BV_10_I:
        cfg->chnl_mode = SBC_JOINT_STEREO;
        cfg->sample_freq = 48000;
        cfg->blocks = 16;
        cfg->subbands = 8;
        cfg->bit_pool = 51;
        cfg->alloc_method = SBC_METHOD_LOUDNESS;
        break;
    case A2DP_SRC_SET_BV_04_I:
    case A2DP_SRC_SET_BV_06_I:
        cfg->chnl_mode = SBC_MONO;
        cfg->sample_freq = 48000;
        cfg->blocks = 16;
        cfg->subbands = 4;
        cfg->bit_pool = 18;
        cfg->alloc_method = SBC_METHOD_SNR;
        break;
    default:
        break;
    }
#endif

    bt_avsrc_sbc_lock();
    cfg->frmsize = bts2_sbc_encode_cfg(cfg->chnl_mode,
                                       cfg->alloc_method,
                                       cfg->sample_freq,
                                       cfg->blocks,
                                       cfg->subbands,
                                       cfg->bit_pool);
    bt_avsrc_sbc_unlock();

    LOG_D("frmsize = %d\n", cfg->frmsize);

    if (cfg->frmsize != 0)
    {
        /* calculate how many sbc frms fit into maximum size payload pkt */
        cfg->frms_per_payload = (U16)((inst->max_frm_size - 14) / cfg->frmsize);
        //cfg->frms_per_payload = 7;

        if (cfg->frms_per_payload > MAX_FRM_PER_PAYLOAD)
            cfg->frms_per_payload = MAX_FRM_PER_PAYLOAD;

        /* calculate the amount of sample bytes used per SBC encode call */
        cfg->bytes_per_encoding = (U16)(cfg->blocks * cfg->chnls * cfg->subbands << 1);

        cfg->samples_per_l2c_frm = cfg->frms_per_payload * (cfg->subbands * cfg->blocks);
        cfg->bytes_to_rd = (2 * cfg->chnls) * cfg->samples_per_l2c_frm;

#ifdef CFG_AV_SHARING
        if (bt_av_get_src_streaming_number() < 1)
#endif
        {
            inst->src_data.m_sec_per_pkt = ((cfg->samples_per_l2c_frm * 1000 * MILLISECOND) / cfg->sample_freq);

            inst->src_data.u_sec_per_pkt = ((cfg->samples_per_l2c_frm * 1000000) / cfg->sample_freq) % 1000;
            inst->src_data.m_sec_time_4_next_pkt = (S32)inst->src_data.m_sec_per_pkt;
            inst->src_data.u_sec_per_pkt_sum = inst->src_data.u_sec_per_pkt;
            inst->src_data.u_sec_sync_pkt_sum = 0;
        }
        return TRUE;
    }
    else
    {
        /* unable to cfg SBC with the given pars */
        return FALSE;
    }
}

void bt_avsrc_cal_time_next_pkt(bts2s_av_inst_data *inst, U8 con_idx, U8 frms, U32 *tmp_m_sec_per_pkt, U32 *tmp_u_sec_per_pkt)
{
    bts2_sbc_cfg *cfg = &inst->con[con_idx].act_cfg;

    *tmp_m_sec_per_pkt = ((frms * (cfg->subbands * cfg->blocks) * 1000 * MILLISECOND) / cfg->sample_freq);
    *tmp_u_sec_per_pkt = ((frms * (cfg->subbands * cfg->blocks) * 1000000) / cfg->sample_freq) % 1000;

    return;
}


/*----------------------------------------------------------------------------*
 *
 * DESCRIPTION:
 *
 * INPUT:
 *      bts2_app_stru *bts2_app_data:
 *
 * OUTPUT:
 *      void.
 *
 * NOTE:
 *      none.
 *
 *----------------------------------------------------------------------------*/
void bt_avsrc_hdl_disc_handler(bts2s_av_inst_data *inst, uint8_t con_idx)
{
#ifdef AUDIO_DATA_TEST
    if (stream)
    {
        fclose(stream);
    }
#endif // AUDIO_DATA_TEST
    inst->con[con_idx].suspend_pending = FALSE;
    inst->con[con_idx].start_pending = FALSE;
    inst->con[con_idx].pending_cmd = 0xff;

#ifdef AUDIO_USING_MANAGER
    if (bt_av_get_src_streaming_number() == 0)
    {
        inst->src_data.stream_frm_time_begin = 0;
        if (send_timer_added == 1)
        {
            bts2_timer_ev_cancel(inst->src_data.tid, NULL, NULL);
            inst->src_data.tid = 0;
            send_timer_added = 0;
        }
        bt_avsrc_sharing_cancel();
    }
#endif

    bt_avsrc_set_start_flag(FALSE);

    bt_avsrc_sbc_lock();
    bts2_sbc_encode_completed();
    bt_avsrc_sbc_unlock();
}



int8_t bt_avsrc_hdl_streaming_start(bts2s_av_inst_data *inst, uint8_t con_idx)
{
#ifdef AUDIO_USING_MANAGER
    if ((inst->con[con_idx].st == avconned_streaming) && (inst->src_data.input_cb != NULL))
        inst->src_data.input_cb(as_callback_cmd_cache_empty, NULL, 0);
#endif
    return 0;

}

BOOL bt_avsrc_start_flag = FALSE;
void bt_avsrc_set_start_flag(BOOL flag)
{
    bt_avsrc_start_flag = flag;
}

BOOL bt_avsrc_get_start_flag(void)
{
    return bt_avsrc_start_flag;
}

/*----------------------------------------------------------------------------*
 *
 * DESCRIPTION:
 *
 * INPUT:
 *      bts2_app_stru *bts2_app_data:
 *
 * OUTPUT:
 *      void.
 *
 * NOTE:
 *      none.
 *
 *----------------------------------------------------------------------------*/
int8_t bt_avsrc_hdl_start_cfm(bts2s_av_inst_data *inst, uint8_t con_idx)
{
    int8_t ret = -1;


    if (bt_avsrc_prepare_sbc(inst, con_idx))
    {
#ifdef AUDIO_DATA_TEST
#if AUDIO_DATA_FROM_PSRAM
        datain = (uint8_t *)INPUT_ADDRESS;

#else
        if (!stream)
        {
            stream = fopen("Test\\D.wav", "rb");

        }
        if (stream)
#endif
        {
            INFO_TRACE("\n -- Begin to send audio data to the peer device!\n");
            bts2_timer_ev_add((U32)inst->src_data.m_sec_per_pkt, bt_avsrc_send_data, 0, inst);
            inst->src_data.stream_frm_time_begin = BTS2_GET_TIME();
        }
#endif // AUDIO_DATA_TEST

        ret = 0;
    }

#if defined(CFG_AV_SRC) && defined(AUDIO_USING_MANAGER) && !defined(CFG_AV_SHARING)
    if (inst->src_data.audio_state != AVSRC_AUDIO_SER_OPEN)
    {
        ret = -1;
    }
#endif

    if (!ret)
    {
        bt_interface_bt_event_notify(BT_NOTIFY_A2DP, BT_NOTIFY_A2DP_START_CFM, NULL, 0);
    }

    return ret;

}



/*----------------------------------------------------------------------------*
 *
 * DESCRIPTION:
 *
 * INPUT:
 *      bts2_app_stru *bts2_app_data:
 *
 * OUTPUT:
 *      void.
 *
 * NOTE:
 *      none.
 *
 *----------------------------------------------------------------------------*/
void bt_avsrc_hdl_abort_ind(bts2s_av_inst_data *inst, uint8_t con_idx)
{
#ifdef AUDIO_DATA_TEST
    if (stream)
    {
        fclose(stream);
    }
#endif //AUDIO_DATA_TEST
}

/*----------------------------------------------------------------------------*
 *
 * DESCRIPTION:
 *
 * INPUT:
 *      bts2_app_stru *bts2_app_data:
 *
 * OUTPUT:
 *      void.
 *
 * NOTE:
 *      none.
 *
 *----------------------------------------------------------------------------*/
void bt_avsrc_hdl_suspend_ind(bts2s_av_inst_data *inst, uint8_t con_idx)
{
    // Do nothing
}

/*----------------------------------------------------------------------------*
 *
 * DESCRIPTION:
 *
 * INPUT:
 *      bts2_app_stru *bts2_app_data:
 *
 * OUTPUT:
 *      void.
 *
 * NOTE:
 *      AV source endcode the audio data and send it to the remote sink,
 *      int the APP, the audio data got from file,
 *
 *----------------------------------------------------------------------------*/
uint8_t bt_avsrc_hdl_start_ind(bts2s_av_inst_data *inst, BTS2S_AV_START_IND *msg, uint8_t con_idx)
{
    U8 res;
    U8 i;
    U8 shdl = 0;


    res = AV_ACPT;

    for (i = 0; i < msg->list_len; i++)
    {
        shdl = *(&msg->first_shdl + i);

        if (shdl == inst->con[con_idx].stream_hdl)
        {
            if (inst->con[con_idx].st == avconned_open)
            {
                /*do prepare again, MTU size might be updated since last time. it should not fail since
                   the SBC pars have alrdy been chked when this fn was called first time */
                if (!bt_avsrc_prepare_sbc(inst, con_idx))
                {
                    break;
                }
            }
            else
            {
                res = AV_BAD_ST;
                break;
            }
        }
        else
        {
            res = AV_BAD_ACP_SEID;
            break;
        }
    }
    if ((res == AV_ACPT) && (i == msg->list_len))
    {
        USER_TRACE(">> accept to start the stream\n");
#ifdef AUDIO_DATA_TEST
        stream = fopen("Test\\D.wav", "rb");
        INFO_TRACE("\n Begain to send audio data to the peer device!\n");
        bts2_timer_ev_add(inst->src_data.m_sec_per_pkt, bt_avsrc_send_data, 0, inst);
        inst->src_data.stream_frm_time_begin = BTS2_GET_TIME();
#endif //AUDIO_DATA_TEST
    }

    return res;
}

/*----------------------------------------------------------------------------*
 *
 * DESCRIPTION:
 *
 * INPUT:
 *      bts2_app_stru *bts2_app_data:
 *
 * OUTPUT:
 *      void.
 *
 * NOTE:
 *      none.
 *
 *----------------------------------------------------------------------------*/

void bt_avrc_close_handler(bts2s_av_inst_data *inst, uint8_t con_idx)
{
#ifdef AUDIO_DATA_TEST
    if (stream)
    {
        fclose(stream);
    }
#endif // AUDIO_DATA_TEST
}

#if 0
/*----------------------------------------------------------------------------*
 *
 * DESCRIPTION:
 *
 * INPUT:
 *      bts2_app_stru *bts2_app_data:
 *
 * OUTPUT:
 *      void.
 *
 * NOTE:
 *      none.
 *
 *----------------------------------------------------------------------------*/
static void bt_avsrc_hdl_close_cfm(bts2_app_stru *bts2_app_data)
{
    bts2s_avsrc_inst_data *inst = global_inst;
    BTS2S_AV_CLOSE_CFM *msg;
    msg = (BTS2S_AV_CLOSE_CFM *)bts2_app_data->recv_msg;
    if (msg->res == AV_ACPT)
    {
        INFO_TRACE(">> accept to release the av stream\n");
        inst->local_seid_info[inst->con[inst->con_idx].local_seid_idx].local_seid.in_use = FALSE;
        inst->con[inst->con_idx].st = avconned;
    }
    else
    {
        INFO_TRACE(">> reject to release the av stream\n");
    }
}
#endif
/*----------------------------------------------------------------------------*
 *
 * DESCRIPTION:
 *
 * INPUT:
 *      bts2_app_stru *bts2_app_data:
 *
 * OUTPUT:
 *      void.
 *
 * NOTE:
 *      none.
 *
 *----------------------------------------------------------------------------*/

#ifdef AUDIO_DATA_TEST
static void bt_avsrc_send_data(U16 m, void *data)
{
    bts2s_av_inst_data *inst = bt_av_get_inst_data();

    U8 *pkt_ptr = NULL;
    U8 *payload_ptr  = NULL;
    U8 *sbc_frm_ptr = NULL;
    U8 *samples_ptr = NULL;
    U8 *tmp_ptr = NULL;
    U8 frms;
    U16 payload_size;
    size_t bytes_rdy;
    U32 actual_time_expired;
    U32 actual_timer_delay;
    int con_idx;
    int numrd;
    bts2_sbc_cfg *act_cfg  = NULL;
    U16 i;
    U16 payload_len;
    con_idx = bt_avsrc_get_plyback_conn(inst);

    if (con_idx == - 1)
        return;

    if (inst->con[con_idx].st == avconned_streaming)
    {
        U32 pcm_pkg_pos;
        U32 sb_pos = 0;
        U32 rd_len = 0;
        U16 pl;

        act_cfg = &inst->con[con_idx].act_cfg;
        payload_size = act_cfg->frms_per_payload * act_cfg->frmsize;
        pkt_ptr = bmalloc(payload_size + AV_FIXED_MEDIA_PKT_HDR_SIZE + 1);
        payload_ptr = sbc_frm_ptr = pkt_ptr + AV_FIXED_MEDIA_PKT_HDR_SIZE;
        frms = 0;
        bytes_rdy = act_cfg->bytes_to_rd;
        tmp_ptr = bmalloc(bytes_rdy);
#if AUDIO_DATA_FROM_PSRAM
        if (((uint32_t)dataend - (uint32_t)datain) < bytes_rdy)
        {
            datain = (uint8_t *)INPUT_ADDRESS;
        }
        memcpy(tmp_ptr, datain, bytes_rdy);
        numrd = bytes_rdy;
        datain += bytes_rdy;
#else
        numrd = fread(tmp_ptr, 1, bytes_rdy, stream); // need some
#endif
        samples_ptr = tmp_ptr;
        *sbc_frm_ptr++ = 0; /*reserve space for payload header */

        pcm_pkg_pos = AV_FIXED_MEDIA_PKT_HDR_SIZE + 1;
        pl = payload_size;

        while (numrd > 0)
        {
            BTS2S_SBC_STREAM bss;

            bss.dst_len = pl;
            bss.pdst = &pkt_ptr[pcm_pkg_pos];
            bss.src_len = numrd;
            bss.psrc = &samples_ptr[sb_pos];

            bts2_sbc_encode(&bss);

            sb_pos += bss.src_len_used;
            rd_len -= bss.src_len_used;

            samples_ptr += bss.src_len_used;
            numrd -= bss.src_len_used;


            pcm_pkg_pos += bss.dst_len_used;
            pl -= bss.dst_len_used;
            frms += bss.dst_len_used / act_cfg->frmsize;
        }

        bfree(tmp_ptr);
        payload_len = (U16)(frms * act_cfg->frmsize + 1 + AV_FIXED_MEDIA_PKT_HDR_SIZE);
        if (frms > 0)
        {
            *payload_ptr = frms;
            for (i = 0; i < MAX_CONNS; i++)
            {
                if (inst->con[i].st == avconned_streaming)
                {
                    U8 *send_pkt_ptr;
                    send_pkt_ptr = bmalloc(payload_len);
                    memcpy(send_pkt_ptr, pkt_ptr, payload_len);
                    av_stream_data_req(inst->con[i].stream_hdl,
                                       FALSE,
                                       FALSE,
                                       96, //any dynamic PT in the range 96 - 127
                                       inst->time_stamp,
                                       payload_len,
                                       send_pkt_ptr); /**/
                    //  INFO_TRACE("con_idx = %d\n", i);
                }
            }
            bfree(pkt_ptr);
            inst->time_stamp += frms * act_cfg->blocks * act_cfg->subbands;
        }
        inst->src_data.stream_frm_time_end = BTS2_GET_TIME();

        actual_time_expired = inst->src_data.stream_frm_time_end - inst->src_data.stream_frm_time_begin;
        actual_timer_delay = actual_time_expired - inst->src_data.m_sec_time_4_next_pkt;
        inst->src_data.m_sec_time_4_next_pkt = inst->src_data.m_sec_per_pkt - actual_timer_delay;
        bts2_timer_ev_add(inst->src_data.m_sec_time_4_next_pkt, bt_avsrc_send_data, 0, inst);
        inst->src_data.stream_frm_time_begin = BTS2_GET_TIME();
    }
}
#else

static uint16_t bt_avsrc_send_data_locked(struct rt_ringbuffer32 *rb)
{
    bts2s_av_inst_data *inst = bt_av_get_inst_data();

    U8 *pkt_ptr = NULL;
    U8 *payload_ptr  = NULL;
    U8 *sbc_frm_ptr = NULL;
    U8 *samples_ptr = NULL;
    U8 *tmp_ptr = NULL;
    U8 frms;
    U16 payload_size;
    size_t bytes_rd = 0;
    U32 actual_time_expired;
    U32 actual_timer_delay;
    int con_idx;
    int numrd;
    bts2_sbc_cfg *act_cfg  = NULL;
    U16 i;
    U16 payload_len;

    uint8_t is_empty = 0;

    con_idx = bt_avsrc_get_plyback_conn(inst);


    if (con_idx == - 1)
        return 0;

    //zhengyu:avoid timer timeout while waiting for suspend cfm
    if (inst->con[con_idx].st == avconned_streaming && !inst->con[con_idx].suspend_pending)
    {
        do
        {
            U32 pcm_pkg_pos;
            U32 sb_pos = 0;
            U32 rd_len = 0;
            U16 pl;
            U16 buffer_count = av_get_stream_buffize();


            if (inst->src_data.stream_frm_time_begin != 0)
            {

                inst->src_data.stream_frm_time_end = BTS2_GET_TIME();

                actual_time_expired = inst->src_data.stream_frm_time_end - inst->src_data.stream_frm_time_begin;
                actual_timer_delay = actual_time_expired - inst->src_data.m_sec_time_4_next_pkt;
                inst->src_data.u_sec_per_pkt_sum += inst->src_data.u_sec_per_pkt;
                inst->src_data.m_sec_time_4_next_pkt = inst->src_data.m_sec_per_pkt - actual_timer_delay;

                if (inst->src_data.u_sec_per_pkt_sum >= (inst->src_data.m_sec_per_pkt * 1000))
                {
                    //USER_TRACE("Cal us %d, ori %d, target %d\n", inst->src_data.u_sec_per_pkt_sum, inst->src_data.m_sec_per_pkt, inst->src_data.m_sec_time_4_next_pkt);
#if 0
                    inst->src_data.m_sec_time_4_next_pkt += inst->src_data.m_sec_per_pkt - 1;
                    inst->src_data.u_sec_per_pkt_sum = 0;
#else
                    inst->src_data.m_sec_time_4_next_pkt += inst->src_data.m_sec_per_pkt;
                    inst->src_data.u_sec_per_pkt_sum = inst->src_data.u_sec_per_pkt_sum % 1000;
#endif
                }


            }

            inst->src_data.stream_frm_time_begin = BTS2_GET_TIME();

            if (buffer_count >= av_get_max_stream_buffer_cnt() / 2)
            {
                LOG_I("a2dp buffer more than half, should send buffered frames,count = %d\n", buffer_count);
                break;
            }


            uint32_t len = rt_ringbuffer32_data_len(rb);

            act_cfg = &inst->con[con_idx].act_cfg;

            // Just return if length is not enough
            if (len < act_cfg->bytes_to_rd)
            {
#ifdef AUDIO_USING_MANAGER
                LOG_I("a2dp_src: buffer empty %d/%d\n", len, act_cfg->bytes_to_rd);

                if (inst->src_data.input_cb == NULL)
                {
                    LOG_I("input_cb = NULL!!!!!!\n");
                    return 0;
                }
                inst->src_data.input_cb(as_callback_cmd_cache_half_empty, NULL, 0);
#endif
                is_empty = 1;
            }

            uint8_t *ptr = bcalloc(1, act_cfg->bytes_to_rd);
            if (!ptr)
                break;

            payload_size = act_cfg->frms_per_payload * act_cfg->frmsize;
            pkt_ptr = bmalloc(payload_size + AV_FIXED_MEDIA_PKT_HDR_SIZE + 1);

            if (!pkt_ptr)
            {
                bfree(ptr);
                break;
            }

            if (!is_empty)
            {
                rt_ringbuffer32_get(rb, ptr, act_cfg->bytes_to_rd);

                len = rt_ringbuffer32_data_len(rb);
#ifdef AUDIO_USING_MANAGER
                if ((rt_ringbuffer32_space_len(rb) > act_cfg->bytes_to_rd)
                        || (len < act_cfg->bytes_to_rd))
                {
                    if (inst->src_data.input_cb)
                    {
                        inst->src_data.input_cb(as_callback_cmd_cache_empty, NULL, 0);
                    }
                }
#endif
            }

            payload_ptr = sbc_frm_ptr = pkt_ptr + AV_FIXED_MEDIA_PKT_HDR_SIZE;
            frms = 0;
            numrd = bytes_rd = act_cfg->bytes_to_rd;

            samples_ptr = ptr;
            *sbc_frm_ptr++ = 0; /*reserve space for payload header */

            pcm_pkg_pos = AV_FIXED_MEDIA_PKT_HDR_SIZE + 1;
            pl = payload_size;

            bt_avsrc_sbc_lock();
            while (numrd > 0)
            {
                BTS2S_SBC_STREAM bss;

                bss.dst_len = pl;
                bss.pdst = &pkt_ptr[pcm_pkg_pos];
                bss.src_len = numrd;
                bss.psrc = &samples_ptr[sb_pos];

                bts2_sbc_encode(&bss);

                sb_pos += bss.src_len_used;
                rd_len -= bss.src_len_used;

                samples_ptr += bss.src_len_used;
                numrd -= bss.src_len_used;


                pcm_pkg_pos += bss.dst_len_used;
                pl -= bss.dst_len_used;
                frms += bss.dst_len_used / act_cfg->frmsize;
            }
            bt_avsrc_sbc_unlock();

            bfree(ptr);
            payload_len = (U16)(frms * act_cfg->frmsize + 1 + AV_FIXED_MEDIA_PKT_HDR_SIZE);
            if (frms > 0)
            {
                *payload_ptr = frms;

                for (U8 i = 0; i < MAX_CONNS; i++)
                {
                    if (inst->con[i].st == avconned_streaming && inst->con[i].cfg == AV_AUDIO_SRC)
                    {
                        U8 *stream_data = bt_avsrc_media_alloc(payload_len);
                        if (stream_data)
                        {
                            memcpy(stream_data, pkt_ptr, payload_len);
                            av_stream_data_req(inst->con[i].stream_hdl,
                                               FALSE,
                                               FALSE,
                                               96, //any dynamic PT in the range 96 - 127
                                               inst->time_stamp,
                                               payload_len,
                                               stream_data); /**/
                        }
                    }
                }
                bfree(pkt_ptr);
                inst->time_stamp += frms * act_cfg->blocks * act_cfg->subbands;
            }

            //bfree(pkt_ptr);
        }
        while (0);

#ifdef AUDIO_USING_MANAGER
        // LOG_D("m_sec_time_4_next_pkt = %d\n", inst->src_data.m_sec_time_4_next_pkt);
        if (inst->src_data.m_sec_time_4_next_pkt > 0)
        {
            send_timer_added = 1;
            inst->src_data.tid = bts2_timer_ev_add(inst->src_data.m_sec_time_4_next_pkt, bt_avsrc_send_cb, 0, (void *)rb);
        }
        else
        {
            send_timer_added = 1;
            inst->src_data.tid = bts2_timer_ev_add(0, bt_avsrc_send_cb, 0, (void *)rb);
        }
#endif
    }

    return bytes_rd;
}

static uint16_t bt_avsrc_send_data(struct rt_ringbuffer32 *rb)
{
    uint16_t bytes_rd;

    bt_avsrc_tx_lock();
    bytes_rd = bt_avsrc_send_data_locked(rb);
    bt_avsrc_tx_unlock();
    return bytes_rd;
}


#ifdef CFG_AV_SHARING
/* One-record peek/commit staging ("L1" ts 提交语义): a record read from the
 * share ring is committed (inst->time_stamp advanced, staging released)
 * only after every sink copy has been handed to av_stream_data_req. A
 * fan-out allocation failure keeps the record staged and retries on the
 * next poll instead of dropping audio with a ts gap. "a2dptx" is the only
 * reader of the share ring (the app kick arms the TX thread instead of
 * touching the ring), so no lock is needed for this state. */
static U8 s_share_pkt_buf[1024];
static U16 s_share_pkt_len = 0;
static U8 s_share_pkt_pending = 0;
static U8 s_share_pkt_fails = 0;

static uint16_t bt_avsrc_sharing_locked(struct rt_ringbuffer32 *rb)
{
    bts2s_av_inst_data *inst = bt_av_get_inst_data();

    U8 *pkt_ptr = NULL;
    size_t bytes_rd = 0;
    U32 actual_time_expired;
    U32 actual_timer_delay;
    int con_idx;
    bts2_sbc_cfg *act_cfg  = NULL;
    U16 pkt_len;

    uint8_t is_update_us = 0;

    con_idx = bt_avsrc_get_stream_conn(inst);


    if (con_idx == - 1)
        return 0;

    if (inst->src_data.discard_data == NULL)
    {
        inst->src_data.discard_data = bmalloc(1024);
    }

    //zhengyu:avoid timer timeout while waiting for suspend cfm
    if (!inst->con[con_idx].suspend_pending)
    {
        do
        {
            U32 pcm_pkg_pos;
            U32 sb_pos = 0;
            U32 rd_len = 0;
            U16 pl;

            /* L1 credit gate: hold the fan-out while the combined media
             * pipeline (both streams' FIFOs + L2CA + HCI ACL buffers) is
             * nearing the controller's 4 ACL buffers. Two signals:
             *   1. av_get_stream_buffize() — accurate for a single sink;
             *   2. s_tx_pipe_copies — our combined fan-out counter with
             *      time-based decay, the only multi-stream signal (the
             *      prebuilt global only tracks one stream at a time).
             * The gate runs before the pacing math so a hold is not
             * counted as one media period; the pipeline draining below
             * the threshold releases the pump on the next 1 ms poll. */
            {
                U8 ncopy_est = 0;
                for (U8 li = 0; li < MAX_CONNS; li++)
                {
                    if (inst->con[li].st == avconned_streaming &&
                        inst->con[li].cfg == AV_AUDIO_SRC)
                        ncopy_est++;
                }
                bt_avsrc_pipe_decay();
                U16 pipe_est = (av_get_stream_buffize() > s_tx_pipe_copies)
                               ? av_get_stream_buffize() : s_tx_pipe_copies;
                if (pipe_est + ncopy_est > BT_AVSRC_TX_PIPE_MAX ||
                    av_get_stream_buffize() >= BT_AVSRC_TX_CREDIT_HOLD)
                {
                    bt_avsrc_sharing_rearm(rb, BT_AVSRC_TX_HOLD_MS);
                    return 0;
                }
            }

            /* Pacing tracks committed packets only: a 1 ms retry poll must
             * not be measured as a 29 ms period or the next delay stretches
             * to ~57 ms and the cadence drifts. */
            if ((inst->src_data.stream_frm_time_begin != 0) && !s_share_pkt_pending)
            {
                inst->src_data.stream_frm_time_end = BTS2_GET_TIME();

                actual_time_expired = inst->src_data.stream_frm_time_end - inst->src_data.stream_frm_time_begin;
                actual_timer_delay = actual_time_expired - inst->src_data.m_sec_time_4_next_pkt;
                inst->src_data.u_sec_per_pkt_sum += inst->src_data.u_sec_per_pkt;
                inst->src_data.m_sec_time_4_next_pkt = inst->src_data.m_sec_per_pkt - actual_timer_delay;

                if (inst->src_data.u_sec_per_pkt_sum >= (inst->src_data.m_sec_per_pkt * 1000))
                {
                    //USER_TRACE("Cal us %d, ori %d, target %d\n", inst->src_data.u_sec_per_pkt_sum, inst->src_data.m_sec_per_pkt, inst->src_data.m_sec_time_4_next_pkt);
#if 0
                    inst->src_data.m_sec_time_4_next_pkt += inst->src_data.m_sec_per_pkt - 1;
                    inst->src_data.u_sec_per_pkt_sum = 0;
#else
                    inst->src_data.m_sec_time_4_next_pkt += inst->src_data.m_sec_per_pkt;
                    inst->src_data.u_sec_per_pkt_sum = inst->src_data.u_sec_per_pkt_sum % 1000;
#endif
                    is_update_us = 1;
                }
            }

            if (!s_share_pkt_pending)
            {
                inst->src_data.stream_frm_time_begin = BTS2_GET_TIME();
            }

            // if ((buffer_count >= av_get_max_stream_buffer_cnt() / 2) && (bt_av_get_src_streaming_number() == 1))
            // {
            //     LOG_I("a2dp buffer more than half, should send buffered frames,count = %d\n", buffer_count);
            //     break;
            // }

            uint16_t len = rt_ringbuffer32_data_len((struct rt_ringbuffer32 *)rb);

            act_cfg = &inst->con[con_idx].act_cfg;

            // LOG_I("len = %d\n",len);

            // Just return if length is not enough. A staged (uncommitted)
            // record always has a payload to retry, so it never takes this
            // path even when the ring itself is drained.
            if (!s_share_pkt_pending &&
                    (len < act_cfg->frmsize * act_cfg->frms_per_payload + 13 + 2))
            {
                static U32 s_empty_log_cnt = 0;

                /* Log every 16th occurrence only: printing on every empty
                 * poll floods the console while the BT task is busy and
                 * makes the starvation worse. */
                if ((s_empty_log_cnt++ & 0x0FU) == 0U)
                {
                    LOG_I("a2dp_transfor: buffer empty\n");
                }

                LOG_D("m_sec_time_4_next_pkt1 = %d\n", inst->src_data.m_sec_time_4_next_pkt);
                if ((inst->src_data.m_sec_time_4_next_pkt < 0) && ((inst->src_data.m_sec_time_4_next_pkt + inst->src_data.m_sec_per_pkt * 20) < 0))
                {
                    inst->src_data.m_sec_time_4_next_pkt = inst->src_data.m_sec_per_pkt;
                }
                /* A negative delay used to convert to a huge U32 and stall
                 * the send timer; retry in 1 ms while the ring is refilled. */
                if (inst->src_data.m_sec_time_4_next_pkt <= 0)
                {
                    inst->src_data.m_sec_time_4_next_pkt = 1;
                }
                bt_avsrc_sharing_rearm(rb, inst->src_data.m_sec_time_4_next_pkt);
                return 0;
            }

            if (!s_share_pkt_pending)
            {
                /* Stage one record. It is only "committed" (time_stamp
                 * advanced, staging released) after every sink copy is
                 * handed to av_stream_data_req below. */
                rt_size_t got;
                got = rt_ringbuffer32_get((struct rt_ringbuffer32 *)rb, (U8 *)&pkt_len, sizeof(pkt_len));

                if ((got != sizeof(pkt_len)) ||
                    (pkt_len == 0U) || (pkt_len > sizeof(s_share_pkt_buf)))
                {
                    LOG_D("bad pkt_len %d (got %d)\n", pkt_len, (int)got);
                    break;
                }

                got = rt_ringbuffer32_get((struct rt_ringbuffer32 *)rb, s_share_pkt_buf, pkt_len);
                if (got != pkt_len)
                {
                    /* Short read: s_share_pkt_buf tail holds stale bytes
                     * from the previous record. Drop this record — the ring
                     * was reset or corrupted between the two get() calls. */
                    LOG_W("share ring short read: want %d got %d\n",
                          pkt_len, (int)got);
                    s_share_pkt_pending = 0;
                    break;
                }

                /* SBC frames start at AV_FIXED_MEDIA_PKT_HDR_SIZE+1 (13).
                 * The first SBC byte MUST be the sync word 0x9C. Anything
                 * else means the record was corrupted in the ring or the
                 * encoder produced garbage — sending it would make the sink
                 * emit a loud click (电流音). Drop and log instead. */
                if (s_share_pkt_buf[AV_FIXED_MEDIA_PKT_HDR_SIZE + 1] != 0x9CU)
                {
                    LOG_W("share record SBC sync corrupt: got 0x%02X, "
                          "len %d, frame_cnt %d\n",
                          s_share_pkt_buf[AV_FIXED_MEDIA_PKT_HDR_SIZE + 1],
                          pkt_len,
                          s_share_pkt_buf[AV_FIXED_MEDIA_PKT_HDR_SIZE]);
                    s_share_pkt_pending = 0;
                    break;
                }

                s_share_pkt_len = pkt_len;
                s_share_pkt_pending = 1;
            }
            pkt_ptr = s_share_pkt_buf;
            pkt_len = s_share_pkt_len;


            // if (inst->con[con_idx].st == avconned_streaming)
            {
                bts2_sbc_cfg *cfg = &inst->con[con_idx].act_cfg;
                U8 nb_sbc_frm = pkt_ptr[12];
                U32 send_ts;
                U8 *copies[MAX_CONNS];
                U8 copy_slot[MAX_CONNS];
                U8 ncopy = 0;
                U8 fanout_ok = 1;

                if (nb_sbc_frm < cfg->frms_per_payload)
                {
                    U32 tmp_m_sec_per_pkt;
                    U32 tmp_u_sec_per_pkt;
                    bt_avsrc_cal_time_next_pkt(inst, con_idx, nb_sbc_frm, &tmp_m_sec_per_pkt, &tmp_u_sec_per_pkt);

                    inst->src_data.u_sec_sync_pkt_sum += inst->src_data.m_sec_per_pkt * 1000 + inst->src_data.u_sec_per_pkt \
                                                         - (tmp_m_sec_per_pkt * 1000 + tmp_u_sec_per_pkt);

                    if (inst->src_data.u_sec_sync_pkt_sum >= (inst->src_data.m_sec_time_4_next_pkt * 1000))
                    {
                        inst->src_data.u_sec_sync_pkt_sum -= (inst->src_data.m_sec_time_4_next_pkt * 1000);
                        inst->src_data.m_sec_time_4_next_pkt = 0;
                        //LOG_D("synchronization time\n");
                    }
                }

                send_ts = inst->time_stamp + nb_sbc_frm * act_cfg->blocks * act_cfg->subbands;

                /* L1 发送缓冲池: allocate every sink copy before touching
                 * any of them, so a single allocation failure costs nothing
                 * and all sinks always see the same timestamps. */
                for (U8 i = 0; i < MAX_CONNS; i++)
                {
                    if (inst->con[i].st == avconned_streaming && inst->con[i].cfg == AV_AUDIO_SRC)
                    {
                        U8 *stream_data = bt_avsrc_media_alloc(pkt_len);
                        if (stream_data == NULL)
                        {
                            s_media_nomem[i]++;
                            fanout_ok = 0;
                            break;
                        }
                        copies[ncopy] = stream_data;
                        copy_slot[ncopy] = i;
                        ncopy++;
                    }
                }

                if (!fanout_ok)
                {
                    while (ncopy > 0)
                    {
                        bfree(copies[--ncopy]);
                    }

                    if (s_share_pkt_fails < BT_AVSRC_TX_FAILS_MAX)
                    {
                        /* L1 ts 提交语义: the record stays staged and
                         * time_stamp is NOT advanced — the same packet is
                         * retried on the next poll, so no RTP ts jump ever
                         * reaches the sink. */
                        s_share_pkt_fails++;
                        bt_avsrc_sharing_rearm(rb, BT_AVSRC_TX_HOLD_MS);
                        return 0;
                    }

                    /* Heap dry for BT_AVSRC_TX_FAILS_MAX polls: drop this
                     * record WITHOUT advancing time_stamp (wire ts stays
                     * continuous; only one frame of PCM is lost) and move
                     * on — freezing the chain would silence the stream. */
                    s_share_pkt_pending = 0;
                    s_share_pkt_fails = 0;
                    break;
                }

                s_share_pkt_fails = 0;
                for (U8 j = 0; j < ncopy; j++)
                {
                    memcpy(copies[j], pkt_ptr, pkt_len);
                    av_stream_data_req(inst->con[copy_slot[j]].stream_hdl,
                                       FALSE,
                                       FALSE,
                                       96, //any dynamic PT in the range 96 - 127
                                       send_ts,
                                       pkt_len,
                                       copies[j]); /**/
                    s_media_req[copy_slot[j]]++;
                }
                /* Every copy above is now inside the stack pipeline; the
                 * combined counter is what the credit gate reads next. */
                s_tx_pipe_copies += ncopy;
                /* Commit: everything past this point is visible to the
                 * sinks, so the timestamp advances and the staging slot is
                 * released exactly here. */
                inst->time_stamp = send_ts;
                s_share_pkt_pending = 0;
            }
        }
        while (0);

        /* Self-rearm on the "a2dptx" thread. Unconditional (was under
         * AUDIO_USING_MANAGER): the sharing chain must keep pacing by
         * itself now that the bts2 send timer is gone. */
        if (inst->src_data.m_sec_time_4_next_pkt > 0)
        {
            bt_avsrc_sharing_rearm(rb, inst->src_data.m_sec_time_4_next_pkt);
        }
        else
        {
            if ((inst->src_data.m_sec_time_4_next_pkt < 0) && ((inst->src_data.m_sec_time_4_next_pkt + inst->src_data.m_sec_per_pkt * 20) < 0))
            {
                inst->src_data.m_sec_time_4_next_pkt = inst->src_data.m_sec_per_pkt;
                bt_avsrc_sharing_rearm(rb, inst->src_data.m_sec_per_pkt);
            }
            else
            {
                /* Catch up at a bounded step instead of an immediate re-fire:
                 * back-to-back sends show up as RTP cadence jitter at the
                 * sink without actually getting the audio there faster. */
                bt_avsrc_sharing_rearm(rb, BT_AVSRC_TX_CATCHUP_MS);
            }
        }
    }

    return bytes_rd;
}

uint16_t bt_avsrc_sharing(struct rt_ringbuffer32 *rb)
{
    uint16_t bytes_rd;

    bt_avsrc_tx_lock();
    bytes_rd = bt_avsrc_sharing_locked(rb);
    bt_avsrc_tx_unlock();
    return bytes_rd;
}
#endif

BOOL bt_avrc_check_stream_state(void)
{
    bts2s_av_inst_data *inst = bt_av_get_inst_data();

    int con_idx;
    con_idx = bt_avsrc_get_plyback_conn(inst);

    if (con_idx == - 1)
        return FALSE;

    if (inst->con[con_idx].st == avconned_streaming)
    {
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}


#endif

#if 0
/*----------------------------------------------------------------------------*
 *
 * DESCRIPTION:
 *
 * INPUT:
 *      bts2_app_stru *bts2_app_data:
 *
 * OUTPUT:
 *      void.
 *
 * NOTE:
 *      none.
 *
 *----------------------------------------------------------------------------*/
static void bt_avsrc_hdl_sts_ind(bts2_app_stru *bts2_app_data)
{
    BTS2S_AV_STS_IND *msg;

    msg = (BTS2S_AV_STS_IND *)bts2_app_data->recv_msg;

    switch (msg->st_type)
    {
    case BTS2MU_AV_ENB_CFM:
        bt_avsrc_hdl_enb_cfm(bts2_app_data);
        break;

    case BTS2MU_AV_DISB_CFM:
        /* bt_avsrc_hdl_disb_cfm(bts2_app_data); */
        break;

    case BTS2MU_AV_CONN_CFM:
        /*bt_avsrc_hdl_conn_cfm(bts2_app_data);*/
        break;

    case BTS2MU_AV_CONN_IND:
        bt_avsrc_hdl_conn_ind(bts2_app_data);
        break;

    default:
        INFO_TRACE(" unhandled STS_IND type \n");
        break;
    }
}
#endif

/*----------------------------------------------------------------------------*
 *
 * DESCRIPTION:
 *
 * INPUT:
 *      bts2_app_stru *bts2_app_data:
 *
 * OUTPUT:
 *      void.
 *
 * NOTE:
 *      none.
 *
 *----------------------------------------------------------------------------*/
void bt_avsrc_init(bts2s_av_inst_data *inst, bts2_app_stru *bts2_app_data)
{
    bt_avsrc_init_data(inst, bts2_app_data);
#ifdef AUDIO_USING_MANAGER
    bt_avsrc_register_audio_service(bts2_app_data);
#endif // AUDIO_USING_MANAGER

#if AUDIO_DATA_FROM_PSRAM
    memcpy((void *)INPUT_ADDRESS, (void *)0x14120000, 0x1AEAA0);
    dataend = (uint8_t *)INPUT_ADDRESS + 0x1AEAA0;
#endif // AUDIO_DATA_FROM_PSRAM
    INFO_TRACE(">> AV source enabled \n");
    av_enb_req(inst->que_id, AV_AUDIO_SRC); //act the svc
}
/*----------------------------------------------------------------------------*
 *
 * DESCRIPTION:
 *
 * INPUT:
 *      bts2_app_stru *bts2_app_data:
 *
 * OUTPUT:
 *      void.
 *
 * NOTE:
 *      none.
 *
 *----------------------------------------------------------------------------*/
void bt_avsrc_msg_handler(bts2_app_stru *bts2_app_data)
{
    U16 *msg_type;
    msg_type = (U16 *)bts2_app_data->recv_msg;
    switch (*msg_type)
    {
    case BTS2MU_AV_SECU_CTRL_CFM:
    {
        INFO_TRACE("<< receive av security contrla confirmation\n");
        break;
    }
    case BTS2MU_AV_SECU_CTRL_IND:
    {
        INFO_TRACE("<< receive av security control indication\n");
        break;
    }
    case BTS2MU_AV_STREAM_DATA_IND:
    {
        INFO_TRACE("<< receive av stream data indication\n");
        break;
    }
#if 0
    case BTS2MU_AV_STS_IND:
    {
        bt_avsrc_hdl_sts_ind(bts2_app_data);
        break;
    }
#endif
    default:
        break;
    }
}
#endif

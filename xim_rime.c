#include <xcb-imdkit/encoding.h>
#include <xcb-imdkit/imdkit.h>
#include <xcb-imdkit/ximproto.h>
#include <rime_api.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <getopt.h>
#include <xcb/xcb.h>
#include <xcb/xcb_aux.h>
#include <xcb/xcb_keysyms.h>
#include <xcb/xproto.h>

#define XIM_RIME_VERSION_STRING    "1.0.0"
#define XIM_RIME_XIM_NAME          "xim_rime"
#define XIM_RIME_APP_NAME          "xim_rime"
#define XIM_RIME_DISTRIBUTION_NAME "Rime"
#define XIM_RIME_DISTRIBUTION_CODE_NAME "FuckME"
#define XIM_RIME_DISTRIBUTION_VERSION XIM_RIME_VERSION_STRING 

// 定义输入法全局状态
typedef struct _IMServerState{
    xcb_connection_t *conn;
    xcb_window_t win_candidate; // 候选词窗口 ID
    xcb_gcontext_t gc;          // 绘制上下文
    RimeApi* rime_api;          // Rime API
    RimeTraits rime_traits;     // Rime Traits
    RimeSessionId rime_session_id; 
    bool is_active;
} IMServerState;


/* options descriptor */
static struct option longopts[] = {
    { "verbose",      no_argument,            NULL,           'v' },
    { "help",         no_argument,            NULL,           'h' },
    { "deplay",       no_argument,            NULL,           'D' },
    { "sync",         no_argument,            NULL,           'S' },
    { "share-data-dir", required_argument,    NULL,           's' },
    { "user-data-dir",  required_argument,    NULL,           'u' },
    { NULL,           0,                      NULL,            0  }
};

#define XIM_RIME_DEFAULT_SHARE_DATA_DIR "/Library/Input Methods/Squirrel.app/Contents/SharedSupport"
#define XIM_RIME_DEFAULT_USER_DATA_DIR  "/Users/michael/.config/xim_rime"

static bool opt_verbose = false;
static bool opt_help    = false;
static bool opt_deplay  = false;
static bool opt_sync    = false;
static char opt_rime_share_data_dir[512] =  XIM_RIME_DEFAULT_SHARE_DATA_DIR;
static char opt_rime_user_data_dir[512]  =  XIM_RIME_DEFAULT_USER_DATA_DIR;


static uint32_t style_array[] = {
    XCB_IM_PreeditNothing | XCB_IM_StatusNothing,
};

static char *encoding_array[] = { "COMPOUND_TEXT" };
static xcb_im_encodings_t encodings = {1, encoding_array};
static xcb_im_styles_t styles = {1, style_array};


static  IMServerState server_state = {0};
static  bool quit_server = false;


static void show_usage()
{
    printf("%s version %s\n", XIM_RIME_APP_NAME, XIM_RIME_VERSION_STRING);
    printf("    usage: %s [option]\n", XIM_RIME_APP_NAME);
    printf("          -v|--verbose: verbose mode\n");
    printf("          -h|--help: show help\n");
    printf("          -s|--share-data-dir: share data directory of Rime, default %s\n", XIM_RIME_DEFAULT_SHARE_DATA_DIR);
    printf("          -u|--user-data-dir: user data directory of Rime, default %s\n", XIM_RIME_DEFAULT_USER_DATA_DIR);
    printf("          -d|--deplay: deplay Rime\n");
    printf("          -S|--sync: sync user data of Rime\n");
}

static void log_printf(const char * restrict format, ...)
{
    va_list ap;

    if(!opt_verbose)
        return;

    printf("[RIME] ");
    va_start(ap, format);
    vprintf(format , ap);
    va_end(ap);
    printf("\n");
}

static void rime_notification_handler(void *context,
                                 RimeSessionId session_id,
                                 const char *message_type,
                                 const char *message_value)
{
    log_printf("%s %s", message_type, message_value);
}

static void server_state_init()
{
    server_state.rime_api = rime_get_api();

    RIME_STRUCT_INIT(RimeTraits, server_state.rime_traits);

    server_state.rime_traits.app_name = XIM_RIME_APP_NAME;
    server_state.rime_traits.distribution_name = XIM_RIME_DISTRIBUTION_NAME;
    server_state.rime_traits.distribution_code_name = XIM_RIME_DISTRIBUTION_CODE_NAME;
    server_state.rime_traits.distribution_version = XIM_RIME_DISTRIBUTION_VERSION;
    server_state.rime_traits.shared_data_dir = opt_rime_share_data_dir; 
    server_state.rime_traits.user_data_dir = opt_rime_user_data_dir;

    log_printf("user data dir %s", server_state.rime_traits.user_data_dir);
    log_printf("shared data dir %s", server_state.rime_traits.shared_data_dir);
    
    server_state.rime_api->setup(&server_state.rime_traits);
    server_state.rime_api->initialize(&server_state.rime_traits);

    server_state.rime_api->set_notification_handler(rime_notification_handler, &server_state);
    server_state.rime_api->start_maintenance(true);
    server_state.rime_api->join_maintenance_thread();
    server_state.rime_session_id = server_state.rime_api->create_session();
    
}

static void server_state_destroy()
{
    if(server_state.rime_api) {
        server_state.rime_api->finalize();
    }
}

/*
// 绘制候选词窗口的函数
void draw_candidate_window(IMServerState *state) {
    if (!state->is_active || state->pinyin_len == 0) {
        // 如果输入法未激活或没输入字母，隐藏窗口
        uint32_t values[] = { XCB_NONE };
        xcb_unmap_window(state->conn, state->win_candidate);
        xcb_flush(state->conn);
        return;
    }

    // 1. 让窗口显示出来
    xcb_map_window(state->conn, state->win_candidate);

    // 2. 清空窗口背景
    xcb_clear_area(state->conn, 0, state->win_candidate, 0, 0, 300, 50);

    // 3. 构造要显示的文本 (拼音 + 候选词)
    char display_text[128];
    const char *cand = lookup_candidate(state->pinyin_buffer);
    snprintf(display_text, sizeof(display_text), "[%s]: %s", state->pinyin_buffer, cand);

    // 4. 使用系统的默认普通字体绘制到窗口上 (XCB 默认只能绘制标准 ASCII/Latin1 字符)
    // 注意：在没有引入当前复杂字体库(Xft/Fontconfig)前，先用英文/数字展示候选结构
    xcb_image_text_8(state->conn, strlen(display_text), state->win_candidate, state->gc, 10, 30, display_text);
    xcb_flush(state->conn);
}

*/

int32_t rime_sync()
{
    int32_t ret_value = 0;

    server_state_init();
    
    log_printf("正在启动同步任务... ");

    // 检查当前引擎是否正忙（如果正在部署或正在同步，需等待）
    if (server_state.rime_api->is_maintenance_mode()) {
        log_printf("引擎当前正忙，请稍后再试。");
        ret_value = 1;
        goto error;
    }

    // 触发同步任务
    if (server_state.rime_api->sync_user_data()) {
        log_printf("同步任务已成功提交到后台。");
        log_printf("sync directory %s", server_state.rime_api->get_sync_dir());
        log_printf("正在双向合并词库并备份配置...");
        server_state.rime_api->join_maintenance_thread();
        log_printf("同步完成！");
    } else {
        log_printf("错误：无法启动同步任务。请检查 installation.yaml 配置。");
        ret_value = 1;
    }
    
error:
    // 释放环境
    server_state_destroy();
    return ret_value;

}

int32_t rime_deplay()
{

    int32_t ret_value = 0;

    log_printf("正在启动部署任务...");    
    server_state_init();
    server_state_destroy();
    return ret_value;
}

void callback(xcb_im_t *im, xcb_im_client_t *client, xcb_im_input_context_t *ic,
              const xcb_im_packet_header_fr_t *hdr, void *frame, void *arg,
              void *user_data) {
/*
    IMServerState *state = (IMServerState *)user_data;
    
    if (hdr->major_opcode == XCB_XIM_DISCONNECT) {
        // end = true;
    }

    if (hdr->major_opcode == XCB_XIM_FORWARD_EVENT) {
        xcb_key_press_event_t *event = arg;
        xcb_key_symbols_t *key_symbols = xcb_key_symbols_alloc(state->conn);
        xcb_keysym_t sym = xcb_key_symbols_get_keysym(key_symbols, event->detail, 0);
        xcb_key_symbols_free(key_symbols);

        // 检测系统发来的激活或关闭通知（在底层被触发键拦截后，XIM 框架会更新状态）
        // 这里我们可以通过显式拦截特定按键或根据框架协议状态管理激活
        
        // 核心打字输入拦截逻辑
        if (sym >= 'a' && sym <= 'z') {
            state->is_active = true; // 临时强制假设已进入激活状态用于展示
            if (state->pinyin_len < sizeof(state->pinyin_buffer) - 1) {
                state->pinyin_buffer[state->pinyin_len++] = (char)sym;
                state->pinyin_buffer[state->pinyin_len] = '\0';
            }
            draw_candidate_window(state);
        } 
        // 用户按下数字键 1 提交第一个候选词
        else if (sym == '1' && state->pinyin_len > 0) {
            const char *commit_text = "测试"; // 模拟选中第一个词提交
            if (strcmp(state->pinyin_buffer, "nihao") == 0) commit_text = "你好";

            size_t len;
            char *result = xcb_utf8_to_compound_text(commit_text, strlen(commit_text), &len);
            xcb_im_commit_string(im, ic, XCB_XIM_LOOKUP_CHARS, result, len, 0);
            free(result);

            // 提交后清空缓冲区并隐藏窗口
            state->pinyin_len = 0;
            state->pinyin_buffer[0] = '\0';
            draw_candidate_window(state);
        }
        // 用户按下 BackSpace 退格键
        else if (sym == XK_BackSpace && state->pinyin_len > 0) {
            state->pinyin_buffer[--state->pinyin_len] = '\0';
            draw_candidate_window(state);
        }
        // 其他控制键，放行给客户端
        else {
            xcb_im_forward_event(im, ic, event);
        }
    }
*/
}


int main(int argc, char *argv[])
{
    int ch;
    int ret_value = 0;
    int screen_default_nbr;
    xcb_screen_t *screen = NULL;
    uint32_t win_candidat_mask = XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL;
    uint32_t win_candidat_values[2] = {0}; // 白色背景，黑色边框
    uint32_t gc_values[2] = {0};
    xcb_im_trigger_keys_t trigger_keys;
    xcb_im_ximtriggerkey_fr_t trigger_key;
    xcb_im_t *im = NULL;
    xcb_window_t w;
    xcb_generic_event_t *event = NULL;
    
    // 解析命令行参数    
    while ((ch = getopt_long(argc, argv, "vhDSs:u:", longopts, NULL)) != -1) {
        switch (ch) {
        case 'v':
            opt_verbose = true;
            break;
        case 'h':
            opt_help = true;
            break;
        case 'D':
            opt_deplay = true;
            break;
        case 'S':
            opt_sync = true;
            break;
        case 's':
            strncpy(opt_rime_share_data_dir, optarg, sizeof(opt_rime_share_data_dir));
            opt_rime_share_data_dir[sizeof(opt_rime_share_data_dir) - 1] = 0;
            break;
        case 'u':
            strncpy(opt_rime_user_data_dir,  optarg, sizeof(opt_rime_user_data_dir));
            opt_rime_user_data_dir[sizeof(opt_rime_user_data_dir) - 1] = 0;
            break;
        }
    }

    if(opt_help) {
        show_usage();
        goto error;   
    }

    if(opt_sync) {
        ret_value = rime_sync();
        goto error;
    }

    if(opt_deplay) {
        ret_value = rime_deplay();
        goto error;        
    }

    // 启动输入法

    xcb_compound_text_init();

    server_state.conn = xcb_connect(NULL, &screen_default_nbr);
    screen = xcb_aux_get_screen(server_state.conn, screen_default_nbr);

    if (!screen) {
        ret_value = 1;
        goto error;
    }

    w = xcb_generate_id(server_state.conn);
    // 1. 创建 XIM 协议底层通信用的隐藏窗口 [1]
    xcb_create_window(server_state.conn,
                      XCB_COPY_FROM_PARENT,
                      w,
                      screen->root,
                      0,
                      0,
                      1,
                      1,
                      1,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT,
                      screen->root_visual,
                      0,
                      NULL);

    // 2. 创建真正的输入法【候选词窗口】 (长 300，高 50)
    server_state.win_candidate = xcb_generate_id(server_state.conn);
    win_candidat_values[0] = screen->white_pixel;
    win_candidat_values[1] = screen->black_pixel; // 白色背景，黑色边框
    
    // 创建一个位于坐标 (200, 200) 的可见弹出窗口
    xcb_create_window(server_state.conn,
                      XCB_COPY_FROM_PARENT,
                      server_state.win_candidate,
                      screen->root, 
                      200, 200, 300, 50, 2,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT,
                      screen->root_visual,
                      win_candidat_mask,
                      win_candidat_values);

    // 创建图形上下文 (GC) 用来绘制文字
    gc_values[0] = screen->black_pixel;
    gc_values[1] = screen->white_pixel;
    server_state.gc = xcb_generate_id(server_state.conn);
    xcb_create_gc(server_state.conn,
                  server_state.gc,
                  server_state.win_candidate,
                  XCB_GC_FOREGROUND | XCB_GC_BACKGROUND,
                  gc_values);

    // 3. 配置触发键 (Ctrl + Space) [1]
    trigger_key.keysym = ' ';
    trigger_key.modifier = 1 << 2;
    trigger_key.modifier_mask = 1 << 2;
    trigger_keys.nKeys = 1;
    trigger_keys.keys = &trigger_key;

    // 创建 IM 实例 [1]
    im = xcb_im_create(
        server_state.conn,
        screen_default_nbr,
        w,
        XIM_RIME_XIM_NAME,
        XCB_IM_ALL_LOCALES,
        &styles,
        &trigger_keys,
        &trigger_keys,
        &encodings,
        0,
        callback, &server_state); // 把 state 传进回调

    if(!xcb_im_open_im(im) ) {
        ret_value = 1;
        goto error;
    }

    log_printf("XIM Server 启动成功。隐藏通信窗口 ID: %u, 候选框窗口 ID: %u", w, server_state.win_candidate);

    ret_value = 0;
    
    while ((event = xcb_wait_for_event(server_state.conn))) {
        xcb_im_filter_event(im, event);
        free(event);
        if (quit_server) break;
    }

error:
    if(im) {
        xcb_im_close_im(im);
        xcb_im_destroy(im);
    }
    if(server_state.conn) {
        xcb_disconnect(server_state.conn);
    }
    return ret_value;
}

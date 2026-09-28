#include <xcb-imdkit/encoding.h>
#include <xcb-imdkit/imdkit.h>
#include <xcb-imdkit/ximproto.h>
#include <xcb/xcb.h>
#include <xcb/xcb_aux.h>
#include <xcb/xcb_keysyms.h>
#include <xcb/xproto.h>
#include <X11/Xft/Xft.h>
#include <rime_api.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <getopt.h>
#include <uthash.h> 

#define XIM_RIME_VERSION_STRING    "1.0.0"
#define XIM_RIME_XIM_NAME          "xim_rime"
#define XIM_RIME_APP_NAME          "xim_rime"
#define XIM_RIME_DISTRIBUTION_NAME "Rime"
#define XIM_RIME_DISTRIBUTION_CODE_NAME "FuckME"
#define XIM_RIME_DISTRIBUTION_VERSION XIM_RIME_VERSION_STRING 

// 每窗口的session
typedef struct _IMClientSession {
    UT_hash_handle hh;
    xcb_im_client_t * client;
    xcb_window_t client_win;
    RimeSessionId rime_session_id;
} IMClientSession;


// 定义输入法全局状态
typedef struct _IMServerState {
    Display *dpy;
    xcb_connection_t *conn;
    xcb_screen_t *screen;
    int32_t screen_nbr;
    XftFont * xft_font;         // Xft 字体对象
    XftColor xft_fg;            // 字体前景色
    XftColor xft_bg;            // 字体背景色
    xcb_window_t win_candidate; // 候选词窗口 ID
    xcb_im_client_t * current_client;
    RimeApi* rime_api;          // Rime API
    RimeTraits rime_traits;     // Rime Traits
    IMClientSession * sessions;
    xcb_key_symbols_t *key_symbols;
} IMServerState;

/* options descriptor */
static struct option longopts[] = {
    { "verbose",      no_argument,            NULL,           'v' },
    { "help",         no_argument,            NULL,           'h' },
    { "font",         required_argument,      NULL,           'f' },
    { "foreground-color",         required_argument,      NULL,           'c' },
    { "background-color",         required_argument,      NULL,           'C' },
    { "deplay",       no_argument,            NULL,           'D' },
    { "sync",         no_argument,            NULL,           'S' },
    { "share-data-dir", required_argument,    NULL,           's' },
    { "user-data-dir",  required_argument,    NULL,           'u' },
    { NULL,           0,                      NULL,            0  }
};

#define XIM_RIME_DEFAULT_SHARE_DATA_DIR "/Library/Input Methods/Squirrel.app/Contents/SharedSupport"
#define XIM_RIME_DEFAULT_USER_DATA_DIR  "/Users/michael/.config/xim_rime"
#define XIM_RIME_DEFAULT_FONT           "San-Serif-14"
#define XIM_RIME_DEFAULT_FG_COLOR       "#000000"
#define XIM_RIME_DEFAULT_BG_COLOR       "#FFFFFF"

static bool opt_verbose = false;
static bool opt_help    = false;
static bool opt_deplay  = false;
static bool opt_sync    = false;
static char opt_rime_share_data_dir[512] =  XIM_RIME_DEFAULT_SHARE_DATA_DIR;
static char opt_rime_user_data_dir[512]  =  XIM_RIME_DEFAULT_USER_DATA_DIR;
static char opt_font[512] = XIM_RIME_DEFAULT_FONT;
static char opt_fg_color[16] = XIM_RIME_DEFAULT_FG_COLOR;
static char opt_bg_color[16] = XIM_RIME_DEFAULT_BG_COLOR;



static uint32_t style_array[] = {
    XCB_IM_PreeditNothing | XCB_IM_StatusNothing,
};

static char *encoding_array[] = { "COMPOUND_TEXT" };
static xcb_im_encodings_t encodings = {1, encoding_array};
static xcb_im_styles_t styles = {1, style_array};


static  IMServerState server_state = {0};
volatile sig_atomic_t quit_server = false; 


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
    printf("          -f|--font: set font, default %s\n", XIM_RIME_DEFAULT_FONT);
    printf("          -c|--foreground-color: set foreground color, default %s\n", XIM_RIME_DEFAULT_FG_COLOR);
    printf("          -C|--background-color: set foreground color, default %s\n", XIM_RIME_DEFAULT_BG_COLOR);
    
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

static void rime_state_init()
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
}

static void rime_state_destroy()
{
    IMClientSession *entry, *tmp;
    if(server_state.rime_api) {
        HASH_ITER(hh, server_state.sessions, entry, tmp) {
            log_printf("Free session ID: %d for Client %p\n", entry->rime_session_id, entry->client);
            HASH_DEL(server_state.sessions, entry);
            server_state.rime_api->destroy_session(entry->rime_session_id);
            free(entry);
        }
        server_state.rime_api->finalize();
        server_state.rime_api = NULL;
        server_state.sessions = NULL;
    }
}


// 动态将候选窗口定位到客户端窗口的左下角
void move_to_client_bottom_left(IMServerState *state, xcb_window_t client_win, uint32_t win_width, uint32_t win_height)
{
    if (client_win == 0) return;

    // 1. 获取客户端窗口的相对几何尺寸 (相对其父窗口的宽、高、相对 X/Y)
    xcb_get_geometry_cookie_t geom_cookie = xcb_get_geometry(state->conn, client_win);
    xcb_get_geometry_reply_t *geom_reply = xcb_get_geometry_reply(state->conn, geom_cookie, NULL);
    
    if (!geom_reply) return;

    uint16_t client_width = geom_reply->width;
    uint16_t client_height = geom_reply->height;
    free(geom_reply);

    // 2. 核心难点：客户端窗口的相对横纵坐标通常是相对于它的父窗口（比如窗口管理器加的边框）
    // 必须调用 translate_coordinates 将客户端窗口的 (0, 0) 原点，翻译为相对于屏幕 Root 窗口的绝对坐标
    xcb_translate_coordinates_cookie_t trans_cookie = xcb_translate_coordinates(
        state->conn, 
        client_win, 
        state->screen->root, 
        0, 0
    );
    xcb_translate_coordinates_reply_t *trans_reply = xcb_translate_coordinates_reply(state->conn, trans_cookie, NULL);

    if (trans_reply) {
        // 绝对坐标的左上角
        int abs_client_x = trans_reply->dst_x;
        int abs_client_y = trans_reply->dst_y;
        free(trans_reply);

        // 3. 计算左下角的绝对位置：
        // X 轴不变（即左对齐）：abs_client_x
        // Y 轴下移（加上客户端自身高度）：abs_client_y + client_height
        int target_x = abs_client_x;
        int target_y = abs_client_y + client_height;

        // 4. 执行窗口重配置（移动位置 + 缩放大小）
        uint32_t config_mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT | XCB_CONFIG_WINDOW_STACK_MODE;
        uint32_t config_values[] = { (uint32_t)target_x, (uint32_t)target_y, win_width, win_height , XCB_STACK_MODE_ABOVE};
        xcb_configure_window(state->conn, state->win_candidate, config_mask, config_values);
    }
}


// 隐藏候选词窗口
void hide_candidate_window(IMServerState *state, IMClientSession *session)
{
    uint32_t config_mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT;
    uint32_t config_values[] = { 0, 0, 1, 1};

    log_printf("hide candidate window for client %p", session->client);
    
    xcb_configure_window(state->conn, state->win_candidate, config_mask, config_values);
    xcb_unmap_window(state->conn, state->win_candidate);
    xcb_flush(state->conn);
}


// 绘制候选词窗口
#define XIM_RIME_MAX_CANDIDATES_STR_LEN (40960)
static char candidates_str[XIM_RIME_MAX_CANDIDATES_STR_LEN];

void draw_candidate_window(IMServerState *state, IMClientSession *session,  RimeContext * context)
{
    size_t candidates_len = 0;
    RimeCandidate candidate;
    XGlyphInfo extents;
    XftDraw *draw = NULL;
    uint32_t config_values[4] = {0};
    uint32_t config_mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT;
    
    int32_t i, w_w = 0, w_h = 0;
    const char * preedit_str = context->composition.preedit;
    
    // 获得 candidates
    for (i = 0; i < context->menu.num_candidates && candidates_len < XIM_RIME_MAX_CANDIDATES_STR_LEN - 1; ++i) {
        candidate = context->menu.candidates[i];
        candidates_len += snprintf( candidates_str + candidates_len,
                                   XIM_RIME_MAX_CANDIDATES_STR_LEN - candidates_len,
                                   "%d. %s ", i + 1, candidate.text);
    }
    candidates_str[candidates_len] = 0;

    log_printf("preedit_str %s", preedit_str ? preedit_str : "");
    log_printf("candidates_str %s", candidates_str);

    if(preedit_str == NULL && candidates_str[0] == 0) {
        hide_candidate_window(state, session);
        return;
    }

    // 记录下当前的client
    state->current_client = session->client;
    
    w_h = state->xft_font->height * 2 + 20;
    
    // 计算窗口w_w
    if(preedit_str) {
        XftTextExtentsUtf8(state->dpy, state->xft_font, (XftChar8 *)preedit_str, strlen(preedit_str), &extents);
        w_w = extents.xOff + 20; 
    }

    XftTextExtentsUtf8(state->dpy, state->xft_font, (XftChar8 *)candidates_str, strlen(candidates_str), &extents);
    if(w_w < extents.xOff + 20)
        w_w = extents.xOff + 20;

    log_printf("w_h = %d w_w = %d", w_h, w_w);

    move_to_client_bottom_left(state, session->client_win, w_w, w_h);      
    xcb_map_window(state->conn, state->win_candidate);
    xcb_flush(state->conn);

    XSync(state->dpy, True); 
    
    //创建 XftDraw 并在窗口上绘制汉字
    draw = XftDrawCreate(state->dpy, (Drawable)state->win_candidate, 
                         DefaultVisual(state->dpy, state->screen_nbr), 
                         DefaultColormap(state->dpy, state->screen_nbr));
    if(draw) {
        XftDrawRect(draw, &state->xft_bg, 0, 0, w_w, w_h);
        if(preedit_str) {
            XftDrawStringUtf8(draw, &state->xft_fg, state->xft_font, 10, state->xft_font->height + 10, (XftChar8 *)preedit_str, strlen(preedit_str));
        }
        if(strlen(candidates_str)) {
            XftDrawStringUtf8(draw, &state->xft_fg, state->xft_font, 10, w_h - 10, (XftChar8 *)candidates_str, strlen(candidates_str));
        }
        XftDrawDestroy(draw);
        XFlush(state->dpy);
    } else {
        log_printf("draw is null");
    }

}



int32_t rime_sync()
{
    int32_t ret_value = 0;

    rime_state_init();
    
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
    rime_state_destroy();
    return ret_value;

}

int32_t rime_deploy()
{

    int32_t ret_value = 0;

    log_printf("正在启动部署任务...");    
    rime_state_init();
    rime_state_destroy();
    log_printf("部署完成");    
    return ret_value;
}

void callback(xcb_im_t *im, xcb_im_client_t *client, xcb_im_input_context_t *ic,
              const xcb_im_packet_header_fr_t *hdr, void *frame, void *arg,
              void *user_data)
{

    IMServerState *state = (IMServerState *)user_data;
    IMClientSession *session = NULL;
    xcb_key_press_event_t *event = arg;
    
    // 尝试获取client session
    HASH_FIND_PTR(state->sessions, &client, session);

    // 如果是新连接，动态创建 Session 并存入管理器
    if(!session && hdr->major_opcode != XCB_XIM_DISCONNECT) {
        session = malloc(sizeof(IMClientSession));
        memset(session, 0, sizeof(IMClientSession));
        session->rime_session_id = state->rime_api->create_session();
        session->client = client;
        HASH_ADD_PTR(state->sessions, client, session);
        log_printf("new session %lu for client %p", session->rime_session_id, client);
    }

    // 删除Session
    if (hdr->major_opcode == XCB_XIM_DISCONNECT) {
        if (session) {
            log_printf("delete session id %lu for %p", session->rime_session_id, session->client);
            HASH_DEL(state->sessions, session);
            if(session->client == state->current_client) {
                 hide_candidate_window(state, session);
            }
            state->rime_api->destroy_session(session->rime_session_id);            
            free(session);
        }
    // 正常打字，将字符流灌入输入法引擎
    } else if (hdr->major_opcode == XCB_XIM_FORWARD_EVENT && session) {
        char * result = NULL;
        size_t result_len;
        RimeCommit commit;
        RimeContext context;
        int rime_mask = 0;
        xcb_key_symbols_t *key_symbols = state->key_symbols;
        xcb_keysym_t keycode =
            xcb_key_symbols_get_keysym(key_symbols, event->detail, 0);

        if (event->state & XCB_MOD_MASK_SHIFT)   rime_mask |= (1 << 0);
        if (event->state & XCB_MOD_MASK_LOCK)    rime_mask |= (1 << 1);
        if (event->state & XCB_MOD_MASK_CONTROL) rime_mask |= (1 << 2);
        if (event->state & XCB_MOD_MASK_1)       rime_mask |= (1 << 3); // 通常对应 Alt/Option 键
        if (event->state & XCB_MOD_MASK_4)       rime_mask |= (1 << 6); // 通常对应 Super/Command 键

        session->client_win = xcb_im_input_context_get_client_window(ic);
        
        if(state->rime_api->process_key(session->rime_session_id, keycode, rime_mask)) {

            RIME_STRUCT_INIT(RimeCommit, commit); // 正确初始化结构体

            // get_commit 如果返回 True，说明上屏文本已经 Ready！
            if (state->rime_api->get_commit(session->rime_session_id, &commit)) {

                
                // 此时文本已 Ready，你可以拿去输出到你的目标文本框中
                log_printf("【上屏文本 Ready】: %s", commit.text);
                result = xcb_utf8_to_compound_text(commit.text,
                                                   strlen(commit.text), &result_len);
                xcb_im_commit_string(im, ic, XCB_XIM_LOOKUP_CHARS, result, result_len, 0);
                // 记得释放内存！
                free(result);
                state->rime_api->free_commit(&commit);

                // 隐藏候选词窗口
                hide_candidate_window(state, session);
                
            } else {
                RIME_STRUCT_INIT(RimeContext, context);
                // 上屏文本没有 Ready，说明用户还在继续输入（组合编码阶段）
                // 此时你可以去 get_context 获取当前的预编辑文字（Preedit）和候选词列表
                if (state->rime_api->get_context(session->rime_session_id, &context)) {
                    if (context.composition.preedit) {
                        log_printf("【预编辑文字 (Preedit)】: %s", context.composition.preedit);
                        log_printf("光标位置: 从 %d 到 %d", context.composition.sel_start, context.composition.sel_end);
                    }
                    
                    log_printf("【候选词列表 (Menu)】(当前页共 %d 个):", context.menu.num_candidates);
                    for (int i = 0; i < context.menu.num_candidates; ++i) {
                        RimeCandidate candidate = context.menu.candidates[i];
                        const char* select_label = (i == context.menu.highlighted_candidate_index) ? "-> " : "   ";
                        log_printf("%s%d. %s (%s)", 
                               select_label, 
                               i + 1, 
                               candidate.text, 
                               candidate.comment ? candidate.comment : "");
                    }
                    log_printf("是否为最后一页: %s", context.menu.is_last_page ? "是" : "否");

                    // 绘制候选词窗口
                    draw_candidate_window(state, session, &context);

                    // 释放内存
                    state->rime_api->free_context(&context);
                }
            } 
        } else if (hdr->major_opcode == XCB_XIM_UNSET_IC_FOCUS && session) {
            hide_candidate_window(state, session);
        } else {
            xcb_im_forward_event(im, ic, event);
        }
    }

}

static struct {
    xcb_connection_t *conn;
    xcb_window_t win;
} sig_bridge = {NULL, 0};


// 信号捕获回调函数
static void handle_signal(int sig)
{
    (void)sig; // 消除未引用的编译器 Warning
    log_printf("Quit!");
    quit_server = true; // 标志位置为 true，通知主循环准备退出    

   // 🌟 核心唤醒逻辑：如果 XCB 连接已经建立，向自己的候选窗口发送一个 ClientMessage
    if (sig_bridge.conn && sig_bridge.win) {
        xcb_client_message_event_t ev = {0};
        ev.response_type = XCB_CLIENT_MESSAGE;
        ev.format = 32;
        ev.window = sig_bridge.win;
        ev.type = XCB_ATOM_ATOM; // 可以是任何合法的 Atom，这里用标准的 ATOM 即可
        
        // 发送事件给自己，强行踢醒阻塞的 xcb_wait_for_event
        xcb_send_event(sig_bridge.conn, false, sig_bridge.win, XCB_EVENT_MASK_NO_EVENT, (const char *)&ev);
        xcb_flush(sig_bridge.conn); // 必须 flush 确保立刻写入网络管道
        log_printf("quit message sent!");
    }
}


int main(int argc, char *argv[])
{
    int ch;
    int ret_value = 0;
    uint32_t win_candidat_mask = XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL | XCB_CW_OVERRIDE_REDIRECT;
    uint32_t win_candidat_values[3] = {0}; // 白色背景，黑色边框
    xcb_im_trigger_keys_t trigger_keys;
    xcb_im_ximtriggerkey_fr_t trigger_key;
    xcb_im_t *im = NULL;
    xcb_window_t w;
    xcb_generic_event_t *event = NULL;
    struct sigaction sa;

    
    // 解析命令行参数    
    while ((ch = getopt_long(argc, argv, "vhDSs:u:f:c:C:", longopts, NULL)) != -1) {
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
        case 'f':
            strncpy(opt_font,  optarg, sizeof(opt_font));
            opt_font[sizeof(opt_font) - 1] = 0;
            break;
        case 'c':
            strncpy(opt_fg_color,  optarg, sizeof(opt_fg_color));
            opt_fg_color[sizeof(opt_fg_color) - 1] = 0;
            break;            
        case 'C':
            strncpy(opt_bg_color,  optarg, sizeof(opt_bg_color));
            opt_bg_color[sizeof(opt_bg_color) - 1] = 0;
            break;            
        }
    }

    if(opt_help) {
        show_usage();
        exit(0);   
    }

    if(opt_sync) {
        ret_value = rime_sync();
        exit(ret_value);
    }

    if(opt_deplay) {
        ret_value = rime_deploy();
        exit(ret_value);
    }


    // 注册信号处理handle
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    
    // 分别拦截 Ctrl+C (SIGINT) 和 kill 命令发出的默认终止信号 (SIGTERM)
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    
    // 启动输入法
    rime_state_init();    
    xcb_compound_text_init();

    // 初始化 Xlib (Xft 运行必须通过 Xlib 建立全局显示环境)
    server_state.dpy = XOpenDisplay(NULL);
    server_state.conn = xcb_connect(NULL, &server_state.screen_nbr);
    server_state.screen = xcb_aux_get_screen(server_state.conn, server_state.screen_nbr);

    if (!server_state.screen) {
        ret_value = 1;
        goto error;
    }

    
    // 加载字体
    server_state.xft_font = XftFontOpenName(server_state.dpy, server_state.screen_nbr, opt_font);
    if (!server_state.xft_font) {
        server_state.xft_font = XftFontOpenName(server_state.dpy, server_state.screen_nbr, XIM_RIME_DEFAULT_FONT);
        if(!server_state.xft_font) {
            log_printf("can not load font, quit!");
            ret_value = 1;
            goto error;
        }
    }

    // 分配前景色（黑色）和背景色（浅灰）
    XftColorAllocName(server_state.dpy,
                      DefaultVisual(server_state.dpy, server_state.screen_nbr),
                      DefaultColormap(server_state.dpy, server_state.screen_nbr),
                      opt_fg_color,
                      &server_state.xft_fg);
    XftColorAllocName(server_state.dpy,
                      DefaultVisual(server_state.dpy, server_state.screen_nbr),
                      DefaultColormap(server_state.dpy, server_state.screen_nbr),
                      opt_bg_color,
                      &server_state.xft_bg);
    
   
    w = xcb_generate_id(server_state.conn);

    //  创建 XIM 协议底层通信用的隐藏窗口 [1]
    xcb_create_window(server_state.conn,
                      XCB_COPY_FROM_PARENT,
                      w,
                      server_state.screen->root,
                      0,
                      0,
                      1,
                      1,
                      1,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT,
                      server_state.screen->root_visual,
                      0,
                      NULL);

    //  创建真正的输入法【候选词窗口】 (长 300，高 50)
    server_state.win_candidate = xcb_generate_id(server_state.conn);
    win_candidat_values[0] = server_state.xft_bg.pixel;
    win_candidat_values[1] = server_state.xft_fg.pixel;
    win_candidat_values[2] = 1;
    
    // 创建一个位于坐标 (200, 200) 的可见弹出窗口
    xcb_create_window(server_state.conn,
                      XCB_COPY_FROM_PARENT,
                      server_state.win_candidate,
                      server_state.screen->root, 
                      0, 0, 1, 1, 1,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT,
                      server_state.screen->root_visual,
                      win_candidat_mask,
                      win_candidat_values);

    xcb_map_window(server_state.conn, server_state.win_candidate);
    xcb_unmap_window(server_state.conn, server_state.win_candidate);
    xcb_flush(server_state.conn);
    XSync(server_state.dpy, False); // 确保预热在主循环开始前被 X 服务器消化完毕

    sig_bridge.conn = server_state.conn;
    sig_bridge.win  = server_state.win_candidate;

    
    // 配置触发键 (Ctrl + Space) [1]
    trigger_key.keysym = ' ';
    trigger_key.modifier = 1 << 2;
    trigger_key.modifier_mask = 1 << 2;
    trigger_keys.nKeys = 1;
    trigger_keys.keys = &trigger_key;
    server_state.key_symbols = xcb_key_symbols_alloc(server_state.conn);
    // 创建 IM 实例 [1]
    im = xcb_im_create(
        server_state.conn,
        server_state.screen_nbr,
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
    rime_state_destroy();
    if(im) {
        xcb_im_close_im(im);
        xcb_im_destroy(im);
    }
    if(server_state.key_symbols) {
        xcb_key_symbols_free(server_state.key_symbols);
        server_state.key_symbols = NULL;
    }
    if(server_state.conn) {
        xcb_disconnect(server_state.conn);
        server_state.conn = NULL;
    }
    if(server_state.dpy) {
        XftColorFree(server_state.dpy,
                     DefaultVisual(server_state.dpy, server_state.screen_nbr),
                     DefaultColormap(server_state.dpy, server_state.screen_nbr),
                     &server_state.xft_fg);
        XftColorFree(server_state.dpy,
                     DefaultVisual(server_state.dpy, server_state.screen_nbr),
                     DefaultColormap(server_state.dpy, server_state.screen_nbr),
                     &server_state.xft_bg);
        if(server_state.xft_font) {
            XftFontClose(server_state.dpy, server_state.xft_font);
            server_state.xft_font = NULL;
        }
        XCloseDisplay(server_state.dpy);
        server_state.dpy = NULL;
    }
    return ret_value;
}

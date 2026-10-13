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
#include <time.h>
#include <uthash.h> 

#define XIM_RIME_VERSION_STRING    "1.0.2"
#define XIM_RIME_XIM_NAME          "xim_rime"
#define XIM_RIME_APP_NAME          "xim_rime"
#define XIM_RIME_DISTRIBUTION_NAME "Rime"
#define XIM_RIME_DISTRIBUTION_CODE_NAME "FuckME"
#define XIM_RIME_DISTRIBUTION_VERSION XIM_RIME_VERSION_STRING 

/* Per-window client session structure */
typedef struct _IMClientSession {
    UT_hash_handle hh;
    xcb_im_client_t * client;
    xcb_window_t client_win;
    RimeSessionId rime_session_id;
} IMClientSession;


/* Input method global server state structure */
typedef struct _IMServerState {
    Display *dpy;
    xcb_connection_t *conn;
    xcb_screen_t *screen;
    int32_t screen_nbr;
    XftFont * xft_font;         /* Xft font object */
    XftColor xft_fg;            /* Font foreground color */
    XftColor xft_bg;            /* Font background color */
    xcb_window_t win_candidate; /* Candidate window window ID */
    xcb_im_client_t * current_client;
    RimeApi* rime_api;          /* Rime API context */
    RimeTraits rime_traits;     /* Rime Traits configuration */
    IMClientSession * sessions;
    xcb_key_symbols_t *key_symbols;
    bool win_candidate_showed;
} IMServerState;

/* Options descriptor */
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
    { "trigger-keys", required_argument,      NULL,           't' },
    { NULL,           0,                      NULL,            0  }
};

#define XIM_RIME_DEFAULT_SHARE_DATA_DIR "/Library/Input Methods/Squirrel.app/Contents/SharedSupport"
#define XIM_RIME_DEFAULT_FONT           "San-Serif-14"
#define XIM_RIME_DEFAULT_FG_COLOR       "#000000"
#define XIM_RIME_DEFAULT_BG_COLOR       "#FFFFFF"
#define  XIM_RIME_DEFAULT_TRIGGER_KEYS  "Control+Space"

static bool opt_verbose = false;
static bool opt_help    = false;
static bool opt_deploy  = false;
static bool opt_sync    = false;
static char opt_rime_share_data_dir[512] =  XIM_RIME_DEFAULT_SHARE_DATA_DIR;
static char opt_rime_user_data_dir[512]  =  {0};
static char opt_font[512] = XIM_RIME_DEFAULT_FONT;
static char opt_fg_color[16] = XIM_RIME_DEFAULT_FG_COLOR;
static char opt_bg_color[16] = XIM_RIME_DEFAULT_BG_COLOR;
static char opt_trigger_keys[32] = XIM_RIME_DEFAULT_TRIGGER_KEYS;


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
    printf("          -d|--deplay: deploy Rime\n");
    printf("          -S|--sync: sync user data of Rime\n");
    printf("          -s|--share-data-dir: share data directory of Rime, default %s\n", XIM_RIME_DEFAULT_SHARE_DATA_DIR);
    printf("          -u|--user-data-dir: user data directory of Rime, required\n");
    printf("          -f|--font: set font, default %s\n", XIM_RIME_DEFAULT_FONT);
    printf("          -c|--foreground-color: set foreground color, default %s\n", XIM_RIME_DEFAULT_FG_COLOR);
    printf("          -C|--background-color: set background color, default %s\n", XIM_RIME_DEFAULT_BG_COLOR);
    printf("          -t|--trigger-keys: set trigger keys (hotkey), default %s\n", XIM_RIME_DEFAULT_TRIGGER_KEYS);
}

static void log_printf(const char * restrict format, ...)
{
    va_list ap;
    struct timespec ts;   
    struct tm time_info;
    
    if(!opt_verbose)
        return;

    clock_gettime(CLOCK_REALTIME, &ts);
    localtime_r(&ts.tv_sec, &time_info);

    fprintf(stderr, "[%04d-%02d-%02d %02d:%02d:%02d.%06ld] [RIME] ",
           time_info.tm_year + 1900,
           time_info.tm_mon + 1,
           time_info.tm_mday,
           time_info.tm_hour,
           time_info.tm_min,
           time_info.tm_sec,
           ts.tv_nsec / 1000);

    va_start(ap, format);
    vfprintf(stderr, format , ap);
    va_end(ap);
    fprintf(stderr, "\n");
}

static void dump_config()
{
    log_printf("          verbose: %d", opt_verbose);
    log_printf("             help: %d", opt_help);
    log_printf("             sync: %d", opt_sync);
    log_printf("           deploy: %d", opt_deploy);
    log_printf("   share-data-dir: %s", opt_rime_share_data_dir);
    log_printf("    user-data-dir: %s", opt_rime_user_data_dir);
    log_printf("     trigger-keys: %s", opt_trigger_keys);
    log_printf("             font: %s", opt_font);
    log_printf(" foreground-color: %s", opt_fg_color);
    log_printf(" background-color: %s", opt_bg_color);
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
            log_printf("Free session %lu for client %p", entry->rime_session_id, entry->client);
            HASH_DEL(server_state.sessions, entry);
            server_state.rime_api->destroy_session(entry->rime_session_id);
            free(entry);
        }
        server_state.rime_api->finalize();
        server_state.rime_api = NULL;
        server_state.sessions = NULL;
    }
}


/* Dynamically position the candidate window to the bottom-left corner of the client window */
static void move_to_client_bottom_left(IMServerState *state, xcb_window_t client_win, uint32_t win_width, uint32_t win_height)
{

    xcb_get_geometry_cookie_t geom_cookie;
    xcb_get_geometry_reply_t *geom_reply = NULL; 
    uint16_t client_height = 0;
    xcb_translate_coordinates_cookie_t trans_cookie;
    xcb_translate_coordinates_reply_t *trans_reply = NULL;
    int32_t abs_client_x = 0; 
    int32_t abs_client_y = 0; 
    int32_t target_x = 0; 
    int32_t target_y = 0; 
    uint32_t config_mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT | XCB_CONFIG_WINDOW_STACK_MODE;
    uint32_t config_values[5] = {0};

    
    if (client_win == 0) return;

    /* 1. Get the relative geometric size of the client window (width, height, relative X/Y to its parent) */
    geom_cookie = xcb_get_geometry(state->conn, client_win);
    geom_reply = xcb_get_geometry_reply(state->conn, geom_cookie, NULL);
    
    if (!geom_reply) return;

    client_height = geom_reply->height;
    free(geom_reply);

    /* 2. Core Difficulty: The relative coordinates of the client window are usually relative to its parent (like the border added by WM).
       We must call translate_coordinates to translate the client window's (0, 0) origin to absolute coordinates relative to the screen Root window. */
    trans_cookie = xcb_translate_coordinates(
        state->conn, 
        client_win, 
        state->screen->root, 
        0, 0
        );
    trans_reply = xcb_translate_coordinates_reply(state->conn, trans_cookie, NULL);

    if (trans_reply) {
        /* Absolute coordinates of top-left corner */
        abs_client_x = trans_reply->dst_x;
        abs_client_y = trans_reply->dst_y;
        free(trans_reply);

        /* 3. Calculate the absolute position of the bottom-left corner:
           X axis remains unchanged (left aligned): abs_client_x
           Y axis shifts down (plus client's own height): abs_client_y + client_height */
        target_x = abs_client_x;
        target_y = abs_client_y + client_height;

        /* 4. Perform window reconfiguration (move position + resize) */
        config_values[0] = (uint32_t)target_x;
        config_values[1] = (uint32_t)target_y;
        config_values[2] = win_width;
        config_values[3] = win_height;
        config_values[4] = XCB_STACK_MODE_ABOVE;

        xcb_configure_window(state->conn, state->win_candidate, config_mask, config_values);
    }
}


/* Hide the candidate window */
static void hide_candidate_window(IMServerState *state, IMClientSession *session)
{
    uint32_t config_mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT;
    uint32_t config_values[] = { 0, 0, 1, 1};

    log_printf("hide candidate window for client %p", session->client);
    
    xcb_configure_window(state->conn, state->win_candidate, config_mask, config_values);
    xcb_unmap_window(state->conn, state->win_candidate);
    xcb_flush(state->conn);
    state->win_candidate_showed = false;
}


/* Draw the candidate window */
#define XIM_RIME_MAX_CANDIDATES_STR_LEN (40960)
#define XIM_RIME_MAX_PREEDIT_STR_LEN    (4096)
static char candidates_str[XIM_RIME_MAX_CANDIDATES_STR_LEN];
static char preedit_str[XIM_RIME_MAX_PREEDIT_STR_LEN];

static void draw_candidate_window(IMServerState *state, IMClientSession *session,  RimeContext * context)
{
    size_t candidates_len = 0;
    RimeCandidate candidate;
    XGlyphInfo extents;
    XftDraw *draw = NULL;
    
    int32_t i, w_w = 0, w_h = 0, preedit_len = 0;

    /*  Compose preeit_str */
    preedit_str[0] = 0;
    if(context->composition.preedit) {
        preedit_len = strlen(context->composition.preedit);
        strncpy(preedit_str, context->composition.preedit, XIM_RIME_MAX_PREEDIT_STR_LEN - 1);
        // add cursor
        if( context->composition.cursor_pos < XIM_RIME_MAX_PREEDIT_STR_LEN - 1) {
            for(i = preedit_len ; i > context->composition.cursor_pos ; i--) {
                preedit_str[i] = preedit_str[i - 1];
            }
            preedit_str[context->composition.cursor_pos] = '|';
        }
        preedit_str[preedit_len + 1] = 0;
        preedit_len ++;
    }

    /* Add page NO. */
    if(context->menu.page_no &&  context->menu.page_size) {
        if(preedit_len < XIM_RIME_MAX_PREEDIT_STR_LEN - 1) {
            preedit_len += snprintf(preedit_str + preedit_len,
                                    XIM_RIME_MAX_PREEDIT_STR_LEN - preedit_len,
                                    "    [page: %d]",
                                    context->menu.page_no);
                }
    }
    
    /* Fetch candidates string */
    candidates_str[0] = 0;
    for (i = 0; i < context->menu.num_candidates && candidates_len < XIM_RIME_MAX_CANDIDATES_STR_LEN - 1; ++i) {
        candidate = context->menu.candidates[i];
        candidates_len += snprintf( candidates_str + candidates_len,
                                    XIM_RIME_MAX_CANDIDATES_STR_LEN - candidates_len,
                                    "%d.%s ", i + 1, candidate.text);
    }

    candidates_str[candidates_len] = 0;

    log_printf("preedit_str %s, cursor_pos %d, page_size %d, page_no %d, last %d", 
               preedit_str, 
               context->composition.cursor_pos,
               context->menu.page_size,
               context->menu.page_no,
               context->menu.is_last_page);
    log_printf("candidates_str %s", candidates_str);

    if(preedit_str[0] == 0 || candidates_str[0] == 0) {
        hide_candidate_window(state, session);
        return;
    }

    state->win_candidate_showed = true;

    /* Record the current active client */
    state->current_client = session->client;
    
    w_h = state->xft_font->height * 2 + 10;
    
    /* Calculate window width (w_w) */
    if(preedit_str[0]) {
        XftTextExtentsUtf8(state->dpy, state->xft_font, (XftChar8 *)preedit_str, strlen(preedit_str), &extents);
        w_w = extents.xOff + 10; 
    }

    XftTextExtentsUtf8(state->dpy, state->xft_font, (XftChar8 *)candidates_str, strlen(candidates_str), &extents);
    if(w_w < extents.xOff + 10)
        w_w = extents.xOff + 10;

    log_printf("w_h = %d w_w = %d", w_h, w_w);

    move_to_client_bottom_left(state, session->client_win, w_w, w_h);      
    xcb_map_window(state->conn, state->win_candidate);
    xcb_flush(state->conn);

    XSync(state->dpy, True); 
    
    /* Create XftDraw canvas and render text onto the window */
    draw = XftDrawCreate(state->dpy, (Drawable)state->win_candidate, 
                         DefaultVisual(state->dpy, state->screen_nbr), 
                         DefaultColormap(state->dpy, state->screen_nbr));
    if(draw) {
        XftDrawRect(draw, &state->xft_bg, 0, 0, w_w, w_h);
        if(preedit_str[0]) {
            XftDrawStringUtf8(draw, &state->xft_fg, state->xft_font, 5, state->xft_font->height - 5, (XftChar8 *)preedit_str, strlen(preedit_str));
        }
        if(candidates_str[0]) {
            XftDrawStringUtf8(draw, &state->xft_fg, state->xft_font, 5, w_h - 5, (XftChar8 *)candidates_str, strlen(candidates_str));
        }
        XftDrawDestroy(draw);
        XFlush(state->dpy);
    } else {
        log_printf("draw is null");
    }

}



static int32_t rime_sync()
{
    int32_t ret_value = 0;

    rime_state_init();
    
    log_printf("starting synchronization task... ");

    /* Check if the engine is busy (if deploying or syncing, we must wait) */
    if (server_state.rime_api->is_maintenance_mode()) {
        log_printf("the engine is currently busy, please try again later.");
        ret_value = 1;
        goto error;
    }

    /* Trigger user data synchronization */
    if (server_state.rime_api->sync_user_data()) {
        log_printf("synchronization task successfully submitted to background.");
        log_printf("sync directory %s", server_state.rime_api->get_sync_dir());
        log_printf("merging dictionaries bi-directionally and backing up configurations...");
        server_state.rime_api->join_maintenance_thread();
        log_printf("synchronization completed successfully!");
    } else {
        log_printf("error: failed to start synchronization task. please verify configuration in installation.yaml.");
        ret_value = 1;
    }
    
error:
    /* Free resources */
    rime_state_destroy();
    return ret_value;

}

static int32_t rime_deploy()
{

    int32_t ret_value = 0;

    log_printf("starting deployment task...");    
    rime_state_init();
    rime_state_destroy();
    log_printf("deployment completed successfully");    
    return ret_value;
}

static void callback(xcb_im_t *im, xcb_im_client_t *client, xcb_im_input_context_t *ic,
              const xcb_im_packet_header_fr_t *hdr, void *frame, void *arg,
              void *user_data)
{

    IMServerState *state = (IMServerState *)user_data;
    IMClientSession *session = NULL;
    xcb_key_press_event_t *event = arg;

    char * result = NULL;
    size_t result_len = 0;
    RimeCommit commit;
    RimeContext context;
    int rime_mask = 0;
    xcb_key_symbols_t *key_symbols = NULL;
    xcb_keysym_t keycode = 0;

    
    /* Attempt to fetch the client session */
    HASH_FIND_PTR(state->sessions, &client, session);

    /* If it is a new connection, dynamically create a Session and store it in the manager */
    if(!session && hdr->major_opcode != XCB_XIM_DISCONNECT) {
        session = malloc(sizeof(IMClientSession));
        memset(session, 0, sizeof(IMClientSession));
        session->rime_session_id = state->rime_api->create_session();
        session->client = client;
        HASH_ADD_PTR(state->sessions, client, session);
        log_printf("new session %lu for client %p", session->rime_session_id, client);
    }

    /* Handle session deletion */
    if (hdr->major_opcode == XCB_XIM_DISCONNECT) {
        if (session) {
            log_printf("delete session %lu for client %p", session->rime_session_id, session->client);
            HASH_DEL(state->sessions, session);
            if(session->client == state->current_client) {
                hide_candidate_window(state, session);
            }
            state->rime_api->destroy_session(session->rime_session_id);            
            free(session);
        }
        /* Normal typing stage: feed key events into input method engine */
    } else if (hdr->major_opcode == XCB_XIM_FORWARD_EVENT && session) {
        key_symbols = state->key_symbols;
        keycode = xcb_key_symbols_get_keysym(key_symbols, event->detail, 0);

        if (event->state & XCB_MOD_MASK_SHIFT)   rime_mask |= (1 << 0);
        if (event->state & XCB_MOD_MASK_LOCK)    rime_mask |= (1 << 1);
        if (event->state & XCB_MOD_MASK_CONTROL) rime_mask |= (1 << 2);
        if (event->state & XCB_MOD_MASK_1)       rime_mask |= (1 << 3); /* Usually maps to Alt/Option key */
        if (event->state & XCB_MOD_MASK_4)       rime_mask |= (1 << 6); /* Usually maps to Super/Command key */

        session->client_win = xcb_im_input_context_get_client_window(ic);

        if(state->rime_api->process_key(session->rime_session_id, keycode, rime_mask)) {

            RIME_STRUCT_INIT(RimeCommit, commit); /* Initialize the commit structure correctly */

            /* If get_commit returns True, it implies the commit text is ready to be committed */
            if (state->rime_api->get_commit(session->rime_session_id, &commit)) {

                
                /* Text is Ready, dispatch it to the target text widget client application */
                log_printf("[commit text ready]: %s", commit.text);
                result = xcb_utf8_to_compound_text(commit.text,
                                                   strlen(commit.text), &result_len);
                xcb_im_commit_string(im, ic, XCB_XIM_LOOKUP_CHARS, result, result_len, 0);
                /* Remember to free allocated string buffer memory */
                free(result);
                state->rime_api->free_commit(&commit);

                /* Hide candidate window */
                hide_candidate_window(state, session);
                
            } else {
                RIME_STRUCT_INIT(RimeContext, context);
                /* Commit text is not ready yet, meaning user is still typing in composition stage */
                /* Query the current composition context to fetch preedit text and candidate listing */
                if (state->rime_api->get_context(session->rime_session_id, &context)) {

                    /* Draw candidate window UI */
                    draw_candidate_window(state, session, &context);

                    /* Free allocated context allocations */
                    state->rime_api->free_context(&context);
                }
            } 
        } else {
            xcb_im_forward_event(im, ic, event);
        }
    } else if (hdr->major_opcode == XCB_XIM_UNSET_IC_FOCUS && session) {
        log_printf("XCB_XIM_UNSET_IC_FOCUS, should hide candidate window!");
        hide_candidate_window(state, session);
    } else if (hdr->major_opcode == XCB_XIM_TRIGGER_NOTIFY && session){
        if(state->win_candidate_showed) {
            log_printf("XCB_XIM_TRIGGER_NOTIFY, should hide candidate window!");
            hide_candidate_window(state, session);
            state->rime_api->clear_composition(session->rime_session_id);
        }
    }

}

static struct {
    xcb_connection_t *conn;
    xcb_window_t win;
} sig_bridge = {NULL, 0};


/* Signal interception hook callback handler */
static void handle_signal(int sig)
{
    (void)sig; /* Eliminate unused parameter compilation compiler warning */
    log_printf("quit!");
    quit_server = true; /* Flip termination flag to true to break out from main loop event engine */    

    /* Core Awakening Trick: If XCB connection is alive, send a dummy ClientMessage to our candidate window */
    if (sig_bridge.conn && sig_bridge.win) {
        xcb_client_message_event_t ev = {0};
        ev.response_type = XCB_CLIENT_MESSAGE;
        ev.format = 32;
        ev.window = sig_bridge.win;
        ev.type = XCB_ATOM_ATOM; /* Any legitimate Atom, fallback to raw ATOM identifier */
        
        /* Push the loop wake event to force unblock xcb_wait_for_event socket reading */
        xcb_send_event(sig_bridge.conn, false, sig_bridge.win, XCB_EVENT_MASK_NO_EVENT, (const char *)&ev);
        xcb_flush(sig_bridge.conn); /* Force flush packets down the X connection pipeline socket */
        log_printf("quit message sent!");
    }
}

static bool parse_trigger_keys(const char * trigger_key_str,
                               xcb_im_trigger_keys_t * trigger_keys,
                               xcb_im_ximtriggerkey_fr_t * trigger_key)
{
    const char * base_key = NULL;

    /* Parse human-readable modifier combinations */
    if (strcasestr(trigger_key_str, "Ctrl") || strcasestr(trigger_key_str, "Control")) {
        trigger_key->modifier |= XCB_MOD_MASK_CONTROL;
        trigger_key->modifier_mask |= XCB_MOD_MASK_CONTROL;
        log_printf("found modifier Ctrl/Control");
    }
    if (strcasestr(trigger_key_str, "Shift")) {
        trigger_key->modifier |= XCB_MOD_MASK_SHIFT;
        trigger_key->modifier_mask |= XCB_MOD_MASK_SHIFT;
        log_printf("found modifier Shift");
    }
    if (strcasestr(trigger_key_str, "Alt") || strcasestr(trigger_key_str, "Meta")) {
        trigger_key->modifier |= XCB_MOD_MASK_1;
        trigger_key->modifier_mask |= XCB_MOD_MASK_1;
        log_printf("found modifier Alt/Meta");

    }
    if (strcasestr(trigger_key_str, "Super") || strcasestr(trigger_key_str, "Win") ||  strcasestr(trigger_key_str, "Command")) {
        trigger_key->modifier |= XCB_MOD_MASK_4;
        trigger_key->modifier_mask |= XCB_MOD_MASK_4;
        log_printf("found modifier Super/Win/Command");

    }

    /* Parse the base keysym symbol component */
    base_key = strrchr(trigger_key_str, '+');
    if (!base_key) {
        base_key = strrchr(trigger_key_str, '-');
    }
    if (base_key) {
        base_key++; /* Advance past the '+' or '-' delimiter symbol */
    } else {
        base_key = trigger_key_str; /* No modifiers present, treat entire block as base key */
    }

    log_printf("using basekey %s...", base_key);

    
    /* Map basic human-readable keywords to X11 Keysym definitions */
    if (strcasecmp(base_key, "Space") == 0) {
        trigger_key->keysym = XK_space; 
        log_printf("found base key Space");        
   } else if (strcasecmp(base_key, "Return") == 0 || strcasecmp(base_key, "Enter") == 0) {
        trigger_key->keysym = XK_Return;
        log_printf("found base key Return/Enter");        
    } else if (strcasecmp(base_key, "Escape") == 0 || strcasecmp(base_key, "Esc") == 0) {
        trigger_key->keysym = XK_Escape;
        log_printf("found base key Escape/Esc");        
    } else if (strlen(base_key) == 1) {
        /* Direct alphanumeric character fallback mapping rules */
        trigger_key->keysym = base_key[0];
        log_printf("found base key %c", base_key[0]); 
    } else {
        /* Ultimate system-wide X11 string-to-keysym lookup fallback rule */
        trigger_key->keysym = XStringToKeysym(base_key);
        log_printf("found base key %c", trigger_key->keysym);
    }
    
    /* Feed layout pointers cleanly back into the input method initializer tracking */
    trigger_keys->nKeys = 1;
    trigger_keys->keys = trigger_key;

    return true;
}

int main(int argc, char *argv[])
{
    int ch;
    int ret_value = 0;
    uint32_t win_candidat_mask = XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL | XCB_CW_OVERRIDE_REDIRECT;
    uint32_t win_candidat_values[3] = {0}; /* White background, black border elements */
    xcb_im_trigger_keys_t trigger_keys = {0};
    xcb_im_ximtriggerkey_fr_t trigger_key = {0};
    xcb_im_t *im = NULL;
    xcb_window_t w;
    xcb_generic_event_t *event = NULL;
    struct sigaction sa;

    
    /* Parse command-line parameters arguments */    
    while ((ch = getopt_long(argc, argv, "vhDSs:u:f:c:C:t:", longopts, NULL)) != -1) {
        switch (ch) {
        case 'v':
            opt_verbose = true;
            break;
        case 'h':
            opt_help = true;
            break;
        case 'D':
            opt_deploy = true;
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
        case 't':
            strncpy(opt_trigger_keys, optarg, sizeof(opt_trigger_keys));
            opt_trigger_keys[sizeof(opt_trigger_keys) - 1] = 0;
        }
    }

    if(opt_help) {
        show_usage();
        exit(0);   
    }

    if(!opt_rime_user_data_dir[0]) {
        printf("error: user data directory required!");
        ret_value = 1;
        exit(ret_value);
    }

    dump_config();

    if(opt_sync) {
        ret_value = rime_sync();
        exit(ret_value);
    }

    if(opt_deploy) {
        ret_value = rime_deploy();
        exit(ret_value);
    }


    /* Setup signal hooks capture layout action */
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    
    /* Trap terminal kill/termination triggers SIGINT and SIGTERM hooks */
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    
    /* Fire up input method engine subsystems */
    rime_state_init();    
    xcb_compound_text_init();

    /* Establish classic Xlib Display handle connection environment (Mandatory for Xft engine tracking) */
    server_state.dpy = XOpenDisplay(NULL);
    server_state.conn = xcb_connect(NULL, &server_state.screen_nbr);
    server_state.screen = xcb_aux_get_screen(server_state.conn, server_state.screen_nbr);

    if (!server_state.screen) {
        log_printf("error: can not connect to screen, quit!");
        ret_value = 1;
        goto error;
    }

    
    /* Instantiate target font framework specs */
    server_state.xft_font = XftFontOpenName(server_state.dpy, server_state.screen_nbr, opt_font);
    if (!server_state.xft_font) {
        log_printf("warn: can not load font %s, using fallback font %s!", opt_font, XIM_RIME_DEFAULT_FONT);
        server_state.xft_font = XftFontOpenName(server_state.dpy, server_state.screen_nbr, XIM_RIME_DEFAULT_FONT);
        if(!server_state.xft_font) {
            log_printf("error: can not load font %s, quit!", opt_font);
            ret_value = 1;
            goto error;
        }
    }

    /* Allocate foreground tracking and background rendering color maps */
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

    /* Create the unmapped underlying hidden communication window required by the XIM protocol standard */
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

    /* Create the actual visible candidate floating popup overlay window object */
    server_state.win_candidate = xcb_generate_id(server_state.conn);
    win_candidat_values[0] = server_state.xft_bg.pixel;
    win_candidat_values[1] = server_state.xft_fg.pixel;
    win_candidat_values[2] = 1;
    
    /* Create a visible popup overlay display window instance */
    xcb_create_window(server_state.conn,
                      XCB_COPY_FROM_PARENT,
                      server_state.win_candidate,
                      server_state.screen->root, 
                      0, 0, 1, 1, 1,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT,
                      server_state.screen->root_visual,
                      win_candidat_mask,
                      win_candidat_values);

    /* Force trigger an initial map/unmap sequence loop sequence to pre-warm X graphic context caches */
    xcb_map_window(server_state.conn, server_state.win_candidate);
    xcb_unmap_window(server_state.conn, server_state.win_candidate);
    xcb_flush(server_state.conn);
    XSync(server_state.dpy, False); /* Flush sync parameters down to clear graphic server queues before entering loop */

    sig_bridge.conn = server_state.conn;
    sig_bridge.win  = server_state.win_candidate;

    /* Parse trigger key */
    if(!parse_trigger_keys(opt_trigger_keys, &trigger_keys, &trigger_key)) {
        log_printf("error: failed to parse hotkey string: %s", opt_trigger_keys);
        ret_value = 1;
        goto error;
    }

    /* Bind the default input method toggle hotspot keys configuration (Control + Space layout) */
    server_state.key_symbols = xcb_key_symbols_alloc(server_state.conn);
    
    /* Instantiate xcb_im server management module node handle */
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
        callback, &server_state); 

    if(!xcb_im_open_im(im) ) {
        ret_value = 1;
        goto error;
    }

    log_printf("XIM iserver initialized successfully. hidden comm window ID: %u, candidate window ID: %u", w, server_state.win_candidate);

    ret_value = 0;
    
    /* Core asynchronous network packet polling loop */
    while ((event = xcb_wait_for_event(server_state.conn))) {
        xcb_im_filter_event(im, event);
        free(event);
        if (quit_server) break;
    }

error:
    /* Clean up all initialized resources gracefully upon exit to prevent ghost leaks */
    if(im) {
        xcb_im_close_im(im);
        xcb_im_destroy(im);
    }
    rime_state_destroy();
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

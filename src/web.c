#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "timing.h"

#include <pthread.h>

#include <libwebsockets.h>

#define WEBSOCKET_OUTPUT_LENGTH 4096
typedef struct {
    uint8_t buffer[LWS_PRE+WEBSOCKET_OUTPUT_LENGTH];
    uint32_t length;
    uint32_t sequence_id;
    pthread_mutex_t mutex;
} websocket_output_t;

websocket_output_t websocket_output_fft_main __attribute__ ((aligned(8))) = {
    .length = 0,
    .sequence_id = 0,
    .mutex = PTHREAD_MUTEX_INITIALIZER
};

void web_submit_fft_main(uint8_t *_data, uint32_t _length)
{
    if(_length > WEBSOCKET_OUTPUT_LENGTH)
    {
        _length = WEBSOCKET_OUTPUT_LENGTH;
    }

    pthread_mutex_lock(&websocket_output_fft_main.mutex);

    websocket_output_fft_main.length = _length;
    memcpy(&(websocket_output_fft_main.buffer[LWS_PRE]), _data, _length * sizeof(uint8_t));
    websocket_output_fft_main.sequence_id++;

    pthread_mutex_unlock(&websocket_output_fft_main.mutex);
}


static const struct lws_http_mount mount = {
    /* .mount_next */       NULL,       /* linked-list "next" */
    /* .mountpoint */       "/",        /* mountpoint URL */
    /* .origin */           "./htdocs", /* serve from dir */
    /* .def */          "index.html",   /* default filename */
    /* .protocol */         NULL,
    /* .cgienv */           NULL,
    /* .extra_mimetypes */      NULL,
    /* .interpret */        NULL,
    /* .cgi_timeout */      0,
    /* .cache_max_age */        0,
    /* .auth_mask */        0,
    /* .cache_reusable */       0,
    /* .cache_revalidate */     0,
    /* .cache_intermediaries */ 0,
    /* .origin_protocol */      LWSMPRO_FILE,   /* files in a dir */
    /* .mountpoint_len */       1,      /* char count */
    /* .basic_auth_login_file */    NULL,
};


/* one of these created for each message */

struct msg {
    void *payload; /* is malloc'd */
    size_t len;
};

/* one of these is created for each client connecting to us */

struct per_session_data__minimal {
    struct per_session_data__minimal *pss_list;
    struct lws *wsi;
    uint32_t last; /* the last message number we sent */
};

/* one of these is created for each vhost our protocol is used with */

struct per_vhost_data__minimal {
    struct lws_context *context;
    struct lws_vhost *vhost;
    const struct lws_protocols *protocol;

    struct per_session_data__minimal *pss_list; /* linked-list of live pss*/

    struct msg amsg; /* the one pending message... */
    int current; /* the current message number we are caching */
};

/* destroys the message when everyone has had a copy of it */

static void
__minimal_destroy_message(void *_msg)
{
    struct msg *msg = _msg;

    free(msg->payload);
    msg->payload = NULL;
    msg->len = 0;
}

static int
callback_fftmain(struct lws *wsi, enum lws_callback_reasons reason,
            void *user, void *in, size_t len)
{
    struct per_session_data__minimal *pss =
            (struct per_session_data__minimal *)user;
    struct per_vhost_data__minimal *vhd =
            (struct per_vhost_data__minimal *)
            lws_protocol_vh_priv_get(lws_get_vhost(wsi),
                    lws_get_protocol(wsi));
    int n;

    switch (reason)
    {
        case LWS_CALLBACK_PROTOCOL_INIT:
            vhd = lws_protocol_vh_priv_zalloc(lws_get_vhost(wsi),
                    lws_get_protocol(wsi),
                    sizeof(struct per_vhost_data__minimal));
            vhd->context = lws_get_context(wsi);
            vhd->protocol = lws_get_protocol(wsi);
            vhd->vhost = lws_get_vhost(wsi);
            break;

        case LWS_CALLBACK_ESTABLISHED:
            /* add ourselves to the list of live pss held in the vhd */
            lws_ll_fwd_insert(pss, pss_list, vhd->pss_list);
            pss->wsi = wsi;
            pss->last = vhd->current;
            break;

        case LWS_CALLBACK_CLOSED:
            /* remove our closing pss from the list of live pss */
            lws_ll_fwd_remove(struct per_session_data__minimal, pss_list, pss, vhd->pss_list);
            break;

        case LWS_CALLBACK_SERVER_WRITEABLE:

            if(websocket_output_fft_main.length == 0)
                break;

            if (pss->last == websocket_output_fft_main.sequence_id)
                break;

            /* Write output data */
            pthread_mutex_lock(&websocket_output_fft_main.mutex);
            n = lws_write(wsi, (unsigned char*)&websocket_output_fft_main.buffer[LWS_PRE], websocket_output_fft_main.length, LWS_WRITE_BINARY);
            if (!n)
            {
                pthread_mutex_unlock(&websocket_output_fft_main.mutex);
                lwsl_err("ERROR %d writing to socket\n", n);
                return -1;
            }
            pss->last = websocket_output_fft_main.sequence_id;
            pthread_mutex_unlock(&websocket_output_fft_main.mutex);

            break;

        case LWS_CALLBACK_RECEIVE:
            if (vhd->amsg.payload)
                __minimal_destroy_message(&vhd->amsg);

            vhd->amsg.len = len;
            /* notice we over-allocate by LWS_PRE */
            vhd->amsg.payload = malloc(LWS_PRE + len);
            if (!vhd->amsg.payload) {
                lwsl_warn("OOM: dropping\n");
                break;
            }

            memcpy((char *)vhd->amsg.payload + LWS_PRE, in, len);
            vhd->current++;

            /*
             * let everybody know we want to write something on them
             * as soon as they are ready
             */
            lws_start_foreach_llp(struct per_session_data__minimal **,
                          ppss, vhd->pss_list) {
                lws_callback_on_writable((*ppss)->wsi);
            } lws_end_foreach_llp(ppss, pss_list);
            break;

        default:
            break;
    }

    return 0;
}

#define LWS_PLUGIN_PROTOCOL_WS_FFT_MAIN \
    { \
        "fft-main", \
        callback_fftmain, \
        sizeof(struct per_session_data__minimal), \
        128, \
        0, NULL, 0 \
    }

enum demo_protocols {
    PROTOCOL_HTTP,
    PROTOCOL_WS_FFT_MAIN,
    _NOP
};
static struct lws_protocols protocols[] = {
    { "http", lws_callback_http_dummy, 0, 0, 0, NULL, 0},
    LWS_PLUGIN_PROTOCOL_WS_FFT_MAIN,
    LWS_PROTOCOL_LIST_TERM
};

static const lws_retry_bo_t retry = {
    .secs_since_valid_ping = 3,
    .secs_since_valid_hangup = 10,
};

static int lws_err = 0;
static pthread_t web_ws_thread_obj;
static struct lws_context *context;
/* Websocket Service Thread */
void *web_ws_thread(void *arg)
{
    bool *exit_requested = (bool *)arg;

    while(!(lws_err < 0) && !(*exit_requested))
    {
        lws_err = lws_service(context, 0);
    }

    pthread_exit(NULL);
}

void *web_thread(void *arg)
{
    bool *exit_requested = (bool *)arg;

    struct lws_context_creation_info info;

    lws_set_log_level(LLL_ERR | LLL_WARN, NULL);
    printf("LWS minimal http server | visit http://localhost:7681\n");

    memset(&info, 0, sizeof info);
    info.port = 7681;
    info.mounts = &mount;
    info.protocols = protocols;
    info.error_document_404 = "/404.html";
    info.options = LWS_SERVER_OPTION_HTTP_HEADERS_SECURITY_BEST_PRACTICES_ENFORCE;
    info.retry_and_idle_policy = &retry;

    context = lws_create_context(&info);
    if (!context)
    {
        lwsl_err("lws init failed\n");
        pthread_exit(NULL);
    }

    /* Fork service off */
    if(pthread_create(&web_ws_thread_obj, NULL, web_ws_thread, (void*)(exit_requested)))
    {
        fprintf(stderr, "Error creating Websocket Service thread\n");
        pthread_exit(NULL);
    }
    pthread_setname_np(web_ws_thread_obj, "Web WS");

    struct timeval tv;
    unsigned int ms, oldms = 0;
    while (!(lws_err < 0) && !(*exit_requested))
    {
        gettimeofday(&tv, NULL);

        ms = (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
        if ((ms - oldms) > 100)
        {
            /* Trigger send on all websockets */
            lws_callback_on_writable_all_protocol(context, &protocols[PROTOCOL_WS_FFT_MAIN]);

            /* Reset timer */
            oldms = ms;
        }
        
        sleep_ms(10);
    }

    lws_context_destroy(context);

    pthread_exit(NULL);
}

#include <stdio.h>
extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libswscale/swscale.h>
    #include <SDL.h>
}

// args
const char *output_filename_prefix;
const char *source_filename;
int view_frame = 0;

//
// global vars
//
  // 1. libav
AVFormatContext *format_ctx = NULL;
int vid_stream_idx = -1;
AVCodecContext *vid_codec_ctx = NULL;
const AVCodec *vid_codec = NULL;
AVFrame *curr_vid_frame = NULL;
AVPacket *curr_pkt = NULL;
int curr_errnum = 0;

  // 2. sdl
SDL_Window *window = NULL;
SDL_Renderer *renderer = NULL;
SDL_Texture *texture = NULL;


  // 3. texture pixel format map
static const struct TextureFormatEntry {
    enum AVPixelFormat format;
    SDL_PixelFormatEnum texture_fmt;
} sdl_texture_format_map[] = {
    { AV_PIX_FMT_RGB8,           SDL_PIXELFORMAT_RGB332 },
    { AV_PIX_FMT_RGB444,         SDL_PIXELFORMAT_RGB444 },
    { AV_PIX_FMT_RGB555,         SDL_PIXELFORMAT_RGB555 },
    { AV_PIX_FMT_BGR555,         SDL_PIXELFORMAT_BGR555 },
    { AV_PIX_FMT_RGB565,         SDL_PIXELFORMAT_RGB565 },
    { AV_PIX_FMT_BGR565,         SDL_PIXELFORMAT_BGR565 },
    { AV_PIX_FMT_RGB24,          SDL_PIXELFORMAT_RGB24 },
    { AV_PIX_FMT_BGR24,          SDL_PIXELFORMAT_BGR24 },
    { AV_PIX_FMT_0RGB32,         SDL_PIXELFORMAT_RGB888 },
    { AV_PIX_FMT_0BGR32,         SDL_PIXELFORMAT_BGR888 },
    { AV_PIX_FMT_NE(RGB0, 0BGR), SDL_PIXELFORMAT_RGBX8888 },
    { AV_PIX_FMT_NE(BGR0, 0RGB), SDL_PIXELFORMAT_BGRX8888 },
    { AV_PIX_FMT_RGB32,          SDL_PIXELFORMAT_ARGB8888 },
    { AV_PIX_FMT_RGB32_1,        SDL_PIXELFORMAT_RGBA8888 },
    { AV_PIX_FMT_BGR32,          SDL_PIXELFORMAT_ABGR8888 },
    { AV_PIX_FMT_BGR32_1,        SDL_PIXELFORMAT_BGRA8888 },
    { AV_PIX_FMT_YUV420P,        SDL_PIXELFORMAT_IYUV },
    { AV_PIX_FMT_YUYV422,        SDL_PIXELFORMAT_YUY2 },
    { AV_PIX_FMT_UYVY422,        SDL_PIXELFORMAT_UYVY },
};

// Converts an AVPixelFormat to an SDL_PixelFormatEnum
// returns SDL_PIXELFORMAT_UNKNOWN if no matching format is found
SDL_PixelFormatEnum pix_fmt_av_to_sdl(enum AVPixelFormat format) {
    for (int i = 0; i < sizeof(sdl_texture_format_map) / sizeof(sdl_texture_format_map[0]); i++) {
        if (sdl_texture_format_map[i].format == format) {
            return sdl_texture_format_map[i].texture_fmt;
        }
    }
    return SDL_PIXELFORMAT_UNKNOWN;
}


void print_err_str(int errnum, const char* err_file, int err_line) {
    if (errnum >= 0) {
        return;
    }
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(errnum, errbuf, AV_ERROR_MAX_STRING_SIZE);
    printf("Error: %s at %s:%d\n", errbuf, err_file, err_line);
    memset(errbuf, 0, AV_ERROR_MAX_STRING_SIZE);
}



int log_av_err(int errnum, const char *file, int line){
    if(errnum < 0) {
        print_err_str(errnum, file, line);
    }
    return errnum;
}

void *log_av_ptr_err(void *ptr,const char *func_call_str, const char *file, int line) {
    if(ptr == NULL) {
        fprintf(stderr, "Error: %s failed to allocate. Pointer is NULL at %s:%d\n", func_call_str, file, line);
    }
    return ptr;
}

#define LOGAVERR(func_call) (log_av_err((func_call), __FILE__, __LINE__))
#define LOGAVPTRERR(ptr, func_call) (log_av_ptr_err((ptr = (func_call), ptr), #func_call, __FILE__, __LINE__))


void print_curr_err_str() {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(curr_errnum, errbuf, AV_ERROR_MAX_STRING_SIZE);
    printf("Error: %s at \n", errbuf);
    memset(errbuf, 0, AV_ERROR_MAX_STRING_SIZE);
}

void close_save_curr_frame_ptrs(AVFrame *rgb_out_frame, FILE *f, SwsContext *sws_ctx) {
    if (f != NULL) fclose(f);
    if (rgb_out_frame != NULL) av_frame_free(&rgb_out_frame);
    if (sws_ctx != NULL) sws_freeContext(sws_ctx);
}


int frame_to_sdltexture() {
    SDL_PixelFormatEnum sdl_pix_fmt = pix_fmt_av_to_sdl((enum AVPixelFormat) curr_vid_frame->format);
    if(sdl_pix_fmt == SDL_PIXELFORMAT_UNKNOWN) {
        fprintf(stderr, "Error: Could not find matching SDL pixel format\n");
        return -1;
    }
    if(texture != NULL) {
        SDL_DestroyTexture(texture);
    }
    if(texture = SDL_CreateTexture(renderer, sdl_pix_fmt, SDL_TEXTUREACCESS_STREAMING, curr_vid_frame->width, curr_vid_frame->height), texture == NULL) {
        fprintf(stderr, "Error: Could not create texture\n");
        return -1;
    }
    // if(SDL_UpdateTexture(texture, NULL, curr_vid_frame->data[0], curr_vid_frame->linesize[0]) < 0) {
    //     fprintf(stderr, "Error: Could not update texture\n");
    //     return -1;
    // }
    return 0;
}

int64_t frame_to_pts(AVStream* pavStream, int frame) {
    return (int64_t(frame) * pavStream->r_frame_rate.den *  pavStream->time_base.den) / (int64_t(pavStream->r_frame_rate.num) * pavStream->time_base.num);
}

int main(int argc, char **argv) {
    // args
    if (argc < 3) {
        printf("Usage: %s <source_filename> <view_frame>\n", argv[0]);
        return 1;
    }
    source_filename = argv[1];
    view_frame = atoi(argv[2]);

    { // sdl initialization
        if(SDL_Init(SDL_INIT_VIDEO) < 0) {
            fprintf(stderr, "Error: Could not initialize SDL\n");
            return 1;
        }
        if(window = SDL_CreateWindow("SDL Window", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1920, 1080, SDL_WINDOW_SHOWN), window == NULL) {
            fprintf(stderr, "Error: Could not create window\n");
            return 1;
        }
        if(renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED), renderer == NULL) {
            fprintf(stderr, "Error: Could not create renderer\n");
            return 1;
        }
    }

    { // get stream info from the input
        // if (CHECK_AV_ERR2(avformat_open_input(&format_ctx, source_filename, NULL, NULL), 1);
        if(LOGAVERR(avformat_open_input(&format_ctx, source_filename, NULL, NULL)) < 0) {
            return 1;
        }

        if(LOGAVERR(avformat_find_stream_info(format_ctx, NULL)) < 0) {
            return 1;
        }
    }

    { // find video stream
        for(int i = 0; i < format_ctx->nb_streams; i++) {
            if(format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                vid_stream_idx = i;
                break;
            }
        }
        if(vid_stream_idx == -1) {
            printf("Error: No video stream found\n");
            return 1;
        }
    }

    { // get video codec and codec context
        if(vid_codec_ctx = avcodec_alloc_context3(NULL), vid_codec_ctx == NULL) {
            printf("Error: Could not allocate codec context\n");
            return 1;
        }
        if(curr_errnum = avcodec_parameters_to_context(vid_codec_ctx, format_ctx->streams[vid_stream_idx]->codecpar), curr_errnum < 0) {
            print_curr_err_str();
            return 1;
        }
        if(vid_codec = avcodec_find_decoder(vid_codec_ctx->codec_id), vid_codec == NULL) {
            printf("Error: Codec not found\n");
            return 1;
        }
        if(curr_errnum = avcodec_open2(vid_codec_ctx, vid_codec, NULL), curr_errnum < 0) {
            print_curr_err_str();
            return 1;
        }
    }

    { // frame and packat alloc
        if(curr_vid_frame = av_frame_alloc(), curr_vid_frame == NULL) {
            printf("Error: Could not allocate frame\n");
            return 1;
        }
        if(curr_pkt = av_packet_alloc(), curr_pkt == NULL) {
            printf("Error: Could not allocate packet\n");
            return 1;
        }
    }

    // get view_frame and save in curr_vid_frame from the video source
    int frame_count = 0;

    int read_frame_err  = 0;
    while( (read_frame_err = av_read_frame(format_ctx, curr_pkt)) >= 0 ) {
            if(curr_pkt->stream_index == vid_stream_idx) {
                if(curr_errnum = avcodec_send_packet(vid_codec_ctx, curr_pkt), curr_errnum < 0) {
                    print_curr_err_str();
                    return 1;
                }
                if(curr_errnum = avcodec_receive_frame(vid_codec_ctx, curr_vid_frame), curr_errnum < 0) {
                    if (curr_errnum != AVERROR(EAGAIN) && curr_errnum != AVERROR_EOF) {
                        print_curr_err_str();
                        return 1;
                    }
                    else if(curr_errnum == AVERROR_EOF) {
                        break;
                    }
                }
            }
    }


    //LOGAVERR(avformat_seek_file(format_ctx, vid_stream_idx, view_frame-1, view_frame, view_frame+1, AVSEEK_FLAG_FRAME));
    int64_t timestamp = av_rescale_q(view_frame, format_ctx->streams[vid_stream_idx]->time_base, AV_TIME_BASE_Q);
    int64_t seek_target = frame_to_pts(format_ctx->streams[vid_stream_idx], view_frame);
    LOGAVERR(av_seek_frame(format_ctx, vid_stream_idx, 10, AVSEEK_FLAG_FRAME | AVSEEK_FLAG_BACKWARD));

    int found_frame = 0;
    { // should just get first frame and map it to sdltexture
        int read_frame_err = 0;
        while( (read_frame_err = av_read_frame(format_ctx, curr_pkt)) >= 0  && !found_frame) {
            if(curr_pkt->stream_index == vid_stream_idx) {
                if(curr_errnum = avcodec_send_packet(vid_codec_ctx, curr_pkt), curr_errnum < 0) {
                    print_curr_err_str();
                    return 1;
                }
                while(curr_errnum = avcodec_receive_frame(vid_codec_ctx, curr_vid_frame), curr_errnum >= 0) {
                    int ret = 0;
                    // if(ret = save_curr_frame(), ret < 0) {
                    //     fprintf(stderr, "Error save_curr_frame()");
                    //     return 1;
                    // }
                    if(ret = frame_to_sdltexture(), ret < 0){
                        fprintf(stderr, "Error frame_to_sdltexture()");
                        return 1;
                    }
                    found_frame = 1;
                    break;
                    av_frame_unref(curr_vid_frame);
                }
            }
            av_packet_unref(curr_pkt);
        }
        if(read_frame_err < 0) {
            curr_errnum = read_frame_err;
            print_curr_err_str();
            //return 1;
        }
    }

    { // just do an sdl loop and diplay texture in renderer
        SDL_Event e;
        int quit = 0;
        while(!quit) {
            while(SDL_PollEvent(&e) != 0) {
                if(e.type == SDL_QUIT) {
                    quit = 1;
                }
            }
            SDL_RenderClear(renderer);
            SDL_UpdateYUVTexture(texture, NULL, 
                curr_vid_frame->data[0], curr_vid_frame->linesize[0],
                curr_vid_frame->data[1], curr_vid_frame->linesize[1],
                curr_vid_frame->data[2], curr_vid_frame->linesize[2]
                );
            SDL_RenderCopy(renderer, texture, NULL, NULL);
            SDL_RenderPresent(renderer);
        }
    }

    { // cleanup
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        av_frame_free(&curr_vid_frame);
        av_packet_free(&curr_pkt);
        avcodec_free_context(&vid_codec_ctx);
        avformat_close_input(&format_ctx);
        SDL_Quit();
    }

    return 0;
}
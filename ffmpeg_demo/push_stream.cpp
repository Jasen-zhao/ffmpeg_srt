/*
将h264格式的视频，进行srt、rtp、udp等推流
代码：https://github.com/sambios/ffmpeg-pusher
*/
#include <iostream>
#include <thread>
#include <chrono>
#include <assert.h>
#include <list>
#include <mutex>

#ifdef __cplusplus 
extern "C" {

#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/avutil.h"
#include "libavutil/time.h"

}
#endif

class FfmpegOutputer {
    enum State {INIT=0, SERVICE, DOWN};
    AVFormatContext *m_ofmt_ctx {nullptr};
    std::string m_url;

    std::thread *m_thread_output {nullptr};
    bool m_thread_output_is_running {false};

    State m_output_state;
    std::mutex m_list_packets_lock;
    std::list<AVPacket*> m_list_packets;
    bool m_repeat {true};

    bool string_start_with(const std::string& s, const std::string& prefix){
        return (s.compare(0, prefix.size(), prefix) == 0);
    }

    int output_initialize(){
        int ret = 0;
        if (!(m_ofmt_ctx->oformat->flags & AVFMT_NOFILE)) {
            //打开输出url
            ret = avio_open(&m_ofmt_ctx->pb, m_url.c_str(), AVIO_FLAG_WRITE);
            if (ret < 0) {
                printf("Could not open output URL '%s'", m_url.c_str());
                return -1;
            }
        }

        AVDictionary *opts = NULL;
        if (string_start_with(m_url, "rtsp://")) {
            //av_dict_set 参数设置:https://blog.csdn.net/weixin_44520287/article/details/119801852
            av_dict_set(&opts, "rtsp_transport", "tcp", 0);
            av_dict_set(&opts, "muxdelay", "0.1", 0);
        }

        //Write file header
        ret = avformat_write_header(m_ofmt_ctx, &opts);
        if (ret < 0) {
            printf("Error occurred when opening output URL\n");
            return -1;
        }

        m_output_state = SERVICE;
        return 0;
    }

    //写入文件内容
    void output_service() {
        int ret = 0;
        while(m_list_packets.size() > 0) {
            AVPacket *pkt = m_list_packets.front();
            m_list_packets.pop_front();
            //写入otput数据包
            ret = av_interleaved_write_frame(m_ofmt_ctx, pkt);
            av_packet_free(&pkt);
            if (ret != 0) {
                std::cout << "av_interleaved_write_frame err" << ret << std::endl;
                m_output_state = DOWN;
                break;
            }
        }

    }
    
    //写入文件尾
    void output_down() {
        if (m_repeat) {
            m_output_state = INIT;
        }else{
            av_write_trailer(m_ofmt_ctx);
            if (!(m_ofmt_ctx->oformat->flags & AVFMT_NOFILE)) {
                avio_closep(&m_ofmt_ctx->pb);
            }

            // Set exit flag
            m_thread_output_is_running = false;
        }
    }

    //打开线程
    void output_process_thread_proc() {
        m_thread_output_is_running = true;
        while(m_thread_output_is_running) {
            switch(m_output_state) {
                case INIT:
                    if (output_initialize() < 0) m_output_state = DOWN;
                    break;
                case SERVICE:
                    output_service();
                    break;
                case DOWN:
                    output_down();
                    break;
            }
        }

        std::cout << "output thread exit!" << std::endl;
    }

public:
    FfmpegOutputer():m_ofmt_ctx(NULL){
        
    }
    virtual ~FfmpegOutputer()
    {
        CloseOutputStream();
    }

    int OpenOutputStream(const std::string& url, AVFormatContext *ifmt_ctx)
    {
        int ret = 0;
        const char* format_name = NULL;
        m_url = url;
        //string_start_with：如果字符串以指定的前缀开始,则返回true;否则返回false
        //format_name：说明推流的封装格式，除了srt以外其它均是默认封装
        if (string_start_with(m_url, "rtsp://")) {
            format_name = "rtsp";
        }else if(string_start_with(m_url, "udp://") || string_start_with(m_url, "tcp://")) {
            format_name = "h264";
        }else if(string_start_with(m_url, "rtp://")) {
            format_name = "rtp";
        }else if(string_start_with(m_url, "rtmp://")) {
            format_name = "flv";
        }else if(string_start_with(m_url, "srt://")) {
            format_name = "h264";
        }else{
            std::cout << "Not support this Url:" << m_url << std::endl;
            return -1;
        }

        if (nullptr == m_ofmt_ctx) {
            ret = avformat_alloc_output_context2(&m_ofmt_ctx, NULL, format_name, m_url.c_str());
            if (ret < 0 || m_ofmt_ctx == NULL) {
                std::cout << "avformat_alloc_output_context2() err=" << ret << std::endl;
                return -1;
            }

            //按ifmt_ctx流的数量给m_ofmt_ctx创造新的流。
            for(int i = 0;i < (int)ifmt_ctx->nb_streams; ++i) {
                printf("new stream %d \n",i);
                AVStream *ostream = avformat_new_stream(m_ofmt_ctx, NULL);
                if (NULL == ostream) {
                    std::cout << "Can't create new stream!" << std::endl;
                    return -1;
                }
#if LIBAVCODEC_VERSION_MAJOR > 56
                ret = avcodec_parameters_copy(ostream->codecpar, ifmt_ctx->streams[i]->codecpar);
                if (ret < 0) {
                    std::cout << "avcodec_parameters_copy() err=" << ret << std::endl;
                    return -1;
                }
#else
                ret = avcodec_copy_context(ostream->codec, ifmt_ctx->streams[i]->codec);
                if (ret < 0){
                    flog(LOG_ERROR, "avcodec_copy_context() err=%d", ret);
                    return -1;
                }
#endif
                // m_ofmt_ctx->oformat->flags |= AVFMT_TS_NONSTRICT;
                if (m_ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
                    ostream->event_flags|= AV_CODEC_FLAG_GLOBAL_HEADER;
                }
            }
        }

        if (!m_ofmt_ctx) {
            printf("Could not create output context\n");
            return -1;
        }

        av_dump_format(m_ofmt_ctx, 0, m_url.c_str(), 1);
        ret = output_initialize();
        if (ret != 0) {
            return -1;
        }

        m_thread_output = new std::thread(&FfmpegOutputer::output_process_thread_proc, this);
        return 0;
    }

    int InputPacket(AVPacket *pkt) {
        AVPacket *pkt1 = av_packet_alloc();
        av_packet_ref(pkt1, pkt);
        m_list_packets.push_back(pkt1);
        return 0;
    }

    int CloseOutputStream() {
        std::cout << "call CloseOutputStream()" << std::endl;
        m_repeat = false;
        m_output_state = DOWN;
        if (m_thread_output) {
            m_thread_output->join();
            delete m_thread_output;
            m_thread_output = nullptr;
        }

        if (m_ofmt_ctx) {
            avformat_free_context(m_ofmt_ctx);
            m_ofmt_ctx = NULL;
        }
        return 0;
    }
};


int main(int argc, char *argv[])
{
    AVFormatContext *ifmt_ctx = NULL;
    AVPacket pkt;
    const char *in_filename, *out_filename;
    FfmpegOutputer *pusher = NULL;
    int ret;
    int frame_index = 0;

    in_filename = "../data/cuc_ieschool.h264";

    //out_filename = "rtmp://172.16.1.25/live/test";
    // out_filename = "rtp://127.0.0.1:1235";
    //out_filename = "tcp://127.0.0.1:9000";
    //out_filename = "udp://127.0.0.1:1235";
    out_filename = "srt://127.0.0.1:1235";

    //Network
    avformat_network_init();

    if ((ret = avformat_open_input(&ifmt_ctx, in_filename, 0, 0)) < 0) {
        printf("Could not open input file.");
        goto end;
    }

    if ((ret = avformat_find_stream_info(ifmt_ctx, 0)) < 0) {
        printf("Failed to retrieve input stream information");
        goto end;
    }

    if (NULL == pusher) {
        pusher = new FfmpegOutputer();
        ret = pusher->OpenOutputStream(out_filename, ifmt_ctx);
        if (ret != 0){
            goto end;
        }
    }
    av_dump_format(ifmt_ctx, 0, in_filename, 0);//打印关于输入或输出格式的详细信息

    while (true) {
        ret = av_read_frame(ifmt_ctx, &pkt);
        if (ret < 0) break;

        if (pkt.pts == AV_NOPTS_VALUE) {
            pkt.dts = pkt.pts = (1.0/30)*90*frame_index;
        }

        pusher->InputPacket(&pkt);
        av_packet_unref(&pkt);
        frame_index++;
        av_usleep(40000);
    }

    end:
    avformat_close_input(&ifmt_ctx);
    avformat_free_context(ifmt_ctx);
    delete pusher;
    return 0;
}
/*
使用ffmpeg采集摄像头数据编码成h265格式并保存成文件
参考网址：https://blog.csdn.net/snail_hunan/article/details/115101715
1、打开设备
2、打开编码器
3、采集摄像头数据，为yuyv422格式
4、转换摄像头数据，从yuyv422转成yuv420p
5、编码yuv420p数据为h264数据，并写成文件
使用命令播放一下：ffplay mycamera.h264

注意，在推流时，给封装的视频流codec是摄像头读出的数据，
由于后面转成了YUV420P并进行了h264编码，
理论上应当将给封装的视频流codec换成h264编码，这里是没换的
*/

#include <iostream>
#include <thread>
#include <chrono>
#include <assert.h>
#include <list>
#include <mutex>
#include <string.h>

#ifdef __cplusplus 
extern "C" {

#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/avutil.h"
#include "libavutil/time.h"
#include "libavdevice/avdevice.h"

}
#endif

//尺寸大小
#define V_WIDTH 640
#define V_HEIGHT 480

//打开设备
static AVFormatContext *open_dev() {
    int ret = 0;
    char errors[1024] = {0,};

    // ctx
    AVFormatContext *fmt_ctx = NULL;
    AVDictionary *options = NULL;

    //摄像头的设备文件
    char *devicename =(char*) "/dev/video0";

    // register video device
    avdevice_register_all();

    //采用video4linux2驱动程序
    AVInputFormat *iformat = (AVInputFormat *)av_find_input_format("video4linux2");

    av_dict_set(&options, "video_size", "640x480", 0);
    av_dict_set(&options, "framerate", "25", 0);
    av_dict_set(&options, "pixel_format", "yuyv422", 0);

    // open device
    ret = avformat_open_input(&fmt_ctx, devicename, iformat, &options);
    if (ret < 0) {
        av_strerror(ret, errors, 1024);
        fprintf(stderr, "Failed to open video device, [%d]%s\n", ret, errors);
        return NULL;
    }

    return fmt_ctx;
}


// 打开编码器
static void open_encoder(int width, int height, AVCodecContext **enc_ctx) {
    int ret = 0;
    AVCodec *codec = NULL;
    avdevice_register_all();//注册所有libavdevice

	codec = (AVCodec *)avcodec_find_encoder_by_name("libx264");
    if (!codec) {
        printf("Codec libx264 not found\n");
        exit(1);
    }

    *enc_ctx = avcodec_alloc_context3(codec);
    if (!enc_ctx) {
        printf("Could not allocate video codec context!\n");
        exit(1);
    }

    // SPS/PPS
    (*enc_ctx)->profile = FF_PROFILE_H264_HIGH_444;//设置压缩的级别,
    (*enc_ctx)->level = 50; //标识level 是5.0 ，level制定了可以最大支持的分辨率和码流

    //设置分辫率
    (*enc_ctx)->width = width;  // 640视频的宽度
    (*enc_ctx)->height = height; // 480视频的高度

    // GOP
    //设置很⼩时，帧就会很多，码流就会很⼤，设置很⼩时，帧就会很少，⼀旦丢包就会出现异常，等到下⼀个帧
    (*enc_ctx)->gop_size = 250;//强相关帧GOP的最大帧数,表示多少帧一个I帧
    //最⼩帧间隔，也就是帧最⼩帧就可以插⼊帧可选项，不设置也可以
    (*enc_ctx)->keyint_min = 25; //如果相邻两帧的变化特别大，可以在gop超过25帧后加入一个I帧

    //设置B帧数据
    (*enc_ctx)->max_b_frames = 3; //b帧最多可以连续3个
    (*enc_ctx)->has_b_frames = 1; //解码过程中帧排序的缓存大小,1

    //参考帧的数量
    (*enc_ctx)->refs = 3; // 参考帧的数量

    //设置输入YUV格式
    (*enc_ctx)->pix_fmt = AV_PIX_FMT_YUV420P;//像素格式

    //设置码率,视频的码率是视频单位时间内传递的数据量;
    //码率越高，视频的清晰度越好;码率越高，视频的也就越大;一般“码率×视频的时间（秒）÷8=视频大小;
    (*enc_ctx)->bit_rate = 600000; //单位kbps,600000

    //设置帧率
    (*enc_ctx)->time_base = (AVRational){1, 25}; //帧与帧之间的间隔是time_base
    (*enc_ctx)->framerate = (AVRational){25, 1}; //帧率，每秒 25帧

    AVDictionary *options =NULL;
    av_dict_set(&options, "tune", "zerolatency", 0);//实时编码，禁止延时;

    ret = avcodec_open2((*enc_ctx), codec, &options);
    if (ret < 0) {
        printf("Could not open codec!\n");
        exit(1);
    }
}


//创建frame
static AVFrame *create_frame(int width, int height) {
    int ret = 0;
    AVFrame *frame = NULL;

    frame = av_frame_alloc();
    if (!frame) {
        printf("Error, No Memory!\n");
        goto __ERROR;
    }

    //设置参数
    frame->width = width;
    frame->height = height;
    frame->format = AV_PIX_FMT_YUV420P;

    // alloc inner memory
    ret = av_frame_get_buffer(frame, 32); //按 32 位对齐
    if (ret < 0) {
        printf("Error, Failed to alloc buffer for frame!\n");
        goto __ERROR;
    }

    return frame;

__ERROR:
    if (frame) {
        av_frame_free(&frame);
    }

    return NULL;
}


// 转换摄像头数据，从yuyv422转成yuv420
void yuyv422ToYuv420p(AVFrame *frame, AVPacket *pkt) {
    int i = 0;
    int yuv422_length = V_WIDTH * V_HEIGHT * 2;
    int y_index = 0;
    // copy all y
    for (i = 0; i < yuv422_length; i += 2) {
        frame->data[0][y_index] = pkt->data[i];
        y_index++;
    }
    // copy u and v
    int line_start = 0;
    // int is_u = 1;
    int u_index = 0;
    int v_index = 0;
    // copy u, v per line. skip a line once
    for (i = 0; i < V_HEIGHT; i += 2) {
        // line i offset
        line_start = i * V_WIDTH * 2;
        for (int j = line_start + 1; j < line_start + V_WIDTH * 2; j += 4) {
            frame->data[1][u_index] = pkt->data[j];
            u_index++;
            frame->data[2][v_index] = pkt->data[j + 2];
            v_index++;
        }
    }
}


//创建推流类
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
                printf("Could not open output URL '%s' \n", m_url.c_str());
                return -1;
            }
        }

        AVDictionary *opts = NULL;
        if (string_start_with(m_url, "srt://")) {
            //av_dict_set 关于延时的参数设置:https://blog.csdn.net/weixin_44520287/article/details/119801852
            av_dict_set(&opts, "srt_transport", "srt", 0);
            av_dict_set(&opts, "max_delay", "1", 0);//设置延迟约束，muxdelay以秒为单位，max_delay以微秒为单位
            av_dict_set(&opts, "tune", "zerolatency", 0);//转码延迟，以牺牲视频质量减少时延
        }

        //Write file header,写文件头
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
    
    //写入文件尾，并关闭线程；
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

    //打开线程，执行写文件头，写流、写文件尾等操作
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
    virtual ~FfmpegOutputer(){
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
        }else if(string_start_with(m_url, "srt://")) {//"mpegts"
            format_name ="h264";
        }else{
            std::cout << "Not support this Url:" << m_url << std::endl;
            return -1;
        }

        if (nullptr == m_ofmt_ctx) {
            //生成推流封装器
            ret = avformat_alloc_output_context2(&m_ofmt_ctx, NULL, format_name, m_url.c_str());
            if (ret < 0 || m_ofmt_ctx == NULL) {
                std::cout << "avformat_alloc_output_context2() err=" << ret << std::endl;
                return -1;
            }
            //向封装器(输出上下文)添加要发送的流（视频、音频），并设置每个流的属性。
            for (int i = 0; i < (int)ifmt_ctx->nb_streams; ++i) {
                if (AVMEDIA_TYPE_DATA != ifmt_ctx->streams[i]->codecpar->codec_type) {
                    AVStream *ostream = avformat_new_stream(m_ofmt_ctx, NULL);
                    if (NULL == ostream) {
                        std::cout << "Can't create new stream!" << std::endl;
                        return -1;
                    }

                    AVCodecContext *codec_ctx = avcodec_alloc_context3(NULL);
                    ret = avcodec_parameters_to_context(codec_ctx, ifmt_ctx->streams[i]->codecpar);
                    if (ret < 0){
                        printf("Failed to copy in_stream codecpar to codec context\n");
                        return -1;
                    }
                    codec_ctx->codec_tag = 0;
                    if (m_ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
                        codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
                    ret = avcodec_parameters_from_context(ostream->codecpar, codec_ctx);
                    if (ret < 0){
                        printf("Failed to copy codec context to out_stream codecpar context\n");
                        return -1;
                    }
                }
            }
            /*向封装器(输出上下文)添加要发送的流（视频、音频），并设置每个流的属性。
            for(int i = 0;i < (int)ifmt_ctx->nb_streams; ++i) {
                printf("new stream %d \n",i);
                AVStream *ostream = avformat_new_stream(m_ofmt_ctx, NULL);
                if (NULL == ostream) {
                    std::cout << "Can't create new stream!" << std::endl;
                    return -1;
                }

                ret = avcodec_parameters_copy(ostream->codecpar, ifmt_ctx->streams[i]->codecpar);
                if (ret < 0) {
                    std::cout << "avcodec_parameters_copy() err=" << ret << std::endl;
                    return -1;
                }
            }*/
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


// 编码并推流
static void encode(AVCodecContext *enc_ctx, AVFrame *frame, AVPacket *newpkt,
                   FfmpegOutputer *pusher) {
    int ret = 0;
    if (frame) {
        printf("send frame to encoder, pts=%ld \n", frame->pts);
    }
    //送原始数据给编码器进行编码,enc_ctx:编码器
    ret = avcodec_send_frame(enc_ctx, frame);
    if (ret < 0) {
        printf("Error, Failed to send a frame for enconding!\n");
        exit(1);
    }
    //从编码器获取编码好的数据
    while (ret >= 0) {
        ret = avcodec_receive_packet(enc_ctx, newpkt);
        //如果编码器数据不足时会返回  EAGAIN,或者到数据尾时会返回 AVERROR_EOF
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            return;
        } else if (ret < 0) {
            printf("Error, Failed to encode!\n");
            exit(1);
        }

        pusher->InputPacket(newpkt);//推流
        av_packet_unref(newpkt);//会将内部相应的引用计数减一
    }
}


int main(int argc, char *argv[]) {
    int base = 0,ret = 0;;//记录帧

    FfmpegOutputer *pusher = NULL;
    AVPacket pkt;
    AVFormatContext *fmt_ctx = NULL;
    AVCodecContext *enc_ctx = NULL;
    const char *out_filename;
    out_filename = "srt://127.0.0.1:1234";

    // 设置日志级别
    av_log_set_level(AV_LOG_DEBUG);

    //打开设备
    fmt_ctx = open_dev();//返回值为设备接口

    //打开编码器
    open_encoder(V_WIDTH, V_HEIGHT, &enc_ctx);

    //创建 AVFrame
    AVFrame *frame = create_frame(V_WIDTH, V_HEIGHT);
    //创建编码后输出的Packet
    AVPacket *newpkt = av_packet_alloc();
    if (!newpkt) {
        printf("Error, Failed to alloc avpacket!\n");
        goto __ERROR;
    }

    //打开推流器
    if (NULL == pusher) {
        pusher = new FfmpegOutputer();
        ret = pusher->OpenOutputStream(out_filename, fmt_ctx);
        if (ret != 0){
            goto __ERROR;
        }
    }
   
    // 从设备读取数据
    while ((ret = av_read_frame(fmt_ctx, &pkt)) == 0) {
        av_log(NULL, AV_LOG_INFO, "packet size is %d(%p)\n", pkt.size,
               pkt.data);
        // YUYVYUYVYUYVYUYV（YUYV422）转成 YYYYYYYYUUVV   （YUV420）
        yuyv422ToYuv420p(frame, &pkt);

        frame->pts = base++;
        encode(enc_ctx, frame, newpkt, pusher);
        
        // release pkt
        av_packet_unref(&pkt); 
        av_usleep(40000);//等待30ms
    }

    encode(enc_ctx, NULL, newpkt, pusher);//间接方法写入文件尾

    __ERROR:
    // close device and release ctx
    if (fmt_ctx) {
        avformat_close_input(&fmt_ctx);
        avformat_free_context(fmt_ctx);
    }
    delete pusher;
    av_log(NULL, AV_LOG_DEBUG, "finish!\n");
    return 0;
}
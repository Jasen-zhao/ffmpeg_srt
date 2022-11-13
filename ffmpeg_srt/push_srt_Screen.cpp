/*
使用ffmpeg采集屏幕数据编码成h264格式并推流
参考网址：https://blog.csdn.net/snail_hunan/article/details/115101715
1、打开设备
2、打开编码器
3、采集屏幕数据，解码为AV_PIX_FMT_BGR0格式的frame
4、转换屏幕数据，将AV_PIX_FMT_BGR0格式的frame转成yuv420p格式的frame
5、编码yuv420p数据编码为h264数据，并封装成mpegts发送

经过测试延时在350ms左右
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
#include <libswscale/swscale.h>

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

    //摄像头的设备文件，命令行参数为-i
    char *devicename =(char*) ":0.0+100,200";//截屏

    // register video device
    avdevice_register_all();

    //采用video4linux2驱动程序,命令行参数为-f
    AVInputFormat *iformat = (AVInputFormat *)av_find_input_format("x11grab");//截屏

    //options其实就是命令行：ffmpeg -framerate 30 -video_size 640x480...
    av_dict_set(&options, "video_size", "640x480", 0);
    av_dict_set(&options, "framerate", "30", 0);

    // open device
    ret = avformat_open_input(&fmt_ctx, devicename, iformat, &options);
    if (ret < 0) {
        av_strerror(ret, errors, 1024);
        fprintf(stderr, "Failed to open video device, [%d]%s\n", ret, errors);
        return NULL;
    }

    return fmt_ctx;
}


// 打开h264编码器，编码的格式是自定义的，因此需要手动配置其参数
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

    /*图像组数量（GOP），一个图像组有一个I帧多个P帧和B帧，I帧、P帧、B帧的数量加在一起为图像组数量
    当接收端靠avformat_find_stream_info获取流的信息来给AVFormatContext作设置时，建议设小一些，不然会等待过长。
    结论：GOP越小越快获得I帧，GOP越大视频越清晰，GOP并不影响延时*/
    (*enc_ctx)->gop_size = 2;//默认250；也可设为2或3，方便接收端avformat_find_stream_info更快的获取pps和sps头；

    //B帧数,两个非B帧之间允许出现多少个B帧数，B帧数不影响画质；
    (*enc_ctx)->max_b_frames = 0; //B帧数越大，编码延时越高，设置为0表示不使用B帧，此时没有编码延时；

    //运动估计参考帧的个数，不影响延时，不影响视频质量；
    (*enc_ctx)->refs = 3; //一般为3

    //设置输入YUV格式
    (*enc_ctx)->pix_fmt = AV_PIX_FMT_YUV420P;//像素格式

    //设置码率,视频的码率是视频单位时间内传递的数据量;1Kbps=1024bps，1Mbps=1024*1024bps
    //码率越高，视频的清晰度越好;清晰度越高，视频的也就越大，编码延时也就越大;一般“码率×视频的时间（秒）÷8=视频大小;
    (*enc_ctx)->bit_rate = 10000000; //单位kbps,这要详细调节才行

    //设置帧率
    (*enc_ctx)->time_base = (AVRational){1, 30}; //帧与帧之间的间隔是time_base
    (*enc_ctx)->framerate = (AVRational){30, 1}; //帧率，每秒 30帧

    AVDictionary *options =NULL;
    //--preset的参数主要调节编码速度和质量的平衡，有ultrafast（转码速度最快，视频往往也最模糊）、
    //superfast、veryfast、faster、fast、medium、slow、slower、veryslow、placebo这10个选项，从快到慢
    av_dict_set(&options, "preset", "ultrafast", 0);//快速模式编码;
    av_dict_set(&options, "tune", "zerolatency", 0);//设置零延时编码
    

    ret = avcodec_open2((*enc_ctx), codec, &options);
    if (ret < 0) {
        printf("Could not open codec!\n");
        exit(1);
    }
}


// 打开解码器，通过AVFormatContext来获取解码器配置，用来将摄像头获取的packet转成frame
static void open_decoder(const AVFormatContext *fmt_ctx,AVCodecContext **dec_ctx,int *videoindex) {
    AVCodec *pCodec = NULL;
    avdevice_register_all();//注册所有libavdevice

    //创建AVCodecContext结构体
	*dec_ctx = avcodec_alloc_context3(NULL);
    if (dec_ctx == NULL){  
		printf("Could not allocate AVCodecContext \n");  
		exit(1); 
    }

    //找到视频流
	for(int i=0; i<(int)fmt_ctx->nb_streams; i++){
		if(fmt_ctx->streams[i]->codecpar->codec_type==AVMEDIA_TYPE_VIDEO)
		{
			(*videoindex)=i;
			break;
		}
	}
	if((*videoindex)==-1){
		printf("Couldn't find a video stream.\n");
		exit(1); 
	}

	//将AVCodecParameters结构体中码流参数拷贝到AVCodecContext结构体
    avcodec_parameters_to_context(*dec_ctx, fmt_ctx->streams[(*videoindex)]->codecpar); 
	
    //选择解码器
	pCodec=(AVCodec *)avcodec_find_decoder((*dec_ctx)->codec_id);
	if(pCodec==NULL){
		printf("Codec not found.\n");
		exit(1);
	}

	//利用pCodec初始化一个音视频编解码器pCodecCtx（AVCodecContext）
	if(avcodec_open2((*dec_ctx), pCodec,NULL)<0){
		printf("Could not open codec.\n");
		exit(1);
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

    int OpenOutputStream(const std::string& url, AVFormatContext *ifmt_ctx,AVCodecContext *enc_ctx)
    {
        int ret = 0;
        const char* format_name = NULL;
        m_url = url;
        //string_start_with：如果字符串以指定的前缀开始,则返回true;否则返回false
        //format_name：说明推流的封装格式，下面均是默认封装
        if (string_start_with(m_url, "rtsp://")) {
            format_name = "rtsp";
        }else if(string_start_with(m_url, "udp://") || string_start_with(m_url, "tcp://")) {
            format_name = "h264";
        }else if(string_start_with(m_url, "rtp://")) {
            format_name = "rtp";
        }else if(string_start_with(m_url, "rtmp://")) {
            format_name = "flv";
        }else if(string_start_with(m_url, "srt://")) {//"mpegts"
            format_name ="mpegts";
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
                    //enc_ctx是h264编码，给输出上下文设置属性
                    AVCodecContext *codec_ctx = enc_ctx;
                    if (NULL==codec_ctx){
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

int main(int argc, char *argv[]) {
    int base = 0,ret = 0,videoindex=-1;

    FfmpegOutputer *pusher = NULL;

    AVFormatContext *fmt_ctx = NULL;//上下文
    AVCodecContext *enc_ctx = NULL;//编码器
    AVCodecContext	*dec_ctx= NULL;//解码器
    const char *out_filename;
    // out_filename = "srt://127.0.0.1:1234";
    out_filename="srt://127.0.0.1:1234?pkt_size=1316";

    // 设置日志级别
    av_log_set_level(AV_LOG_DEBUG);

    //打开设备
    fmt_ctx = open_dev();//返回值为设备接口

    //打开解码器
    open_decoder(fmt_ctx,&dec_ctx,&videoindex);

    //打开编码器
    open_encoder(V_WIDTH, V_HEIGHT, &enc_ctx);

    //定义转换格式的上下文,辅助sws_scale对格式进行转换
    // img_convert_ctx = sws_getContext(V_WIDTH,V_HEIGHT,AV_PIX_FMT_BGR0,V_WIDTH,V_HEIGHT,AV_PIX_FMT_YUV420P,SWS_BICUBIC, NULL, NULL, NULL);
	struct SwsContext * img_convert_ctx= sws_getContext(dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt, dec_ctx->width, dec_ctx->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL); 

    //打开推流器
    if (NULL == pusher) {
        pusher = new FfmpegOutputer();
        ret = pusher->OpenOutputStream(out_filename, fmt_ctx,enc_ctx);
        if (ret != 0){
            goto __ERROR;
        }
    }

    //创建 AVFrame和AVPacket等参数
    AVPacket *newpkt,*pkt;
    AVFrame	*pFrame,*pFrameYUV420;
    pFrame=create_frame(V_WIDTH, V_HEIGHT);
    pFrameYUV420 = create_frame(V_WIDTH, V_HEIGHT);

    //创建编码后输出的Packet
    pkt = av_packet_alloc();
    if (!pkt) {
        printf("Error, Failed to alloc avpacket!\n");
        goto __ERROR;
    }
    newpkt = av_packet_alloc();
    if (!newpkt) {
        printf("Error, Failed to alloc avpacket!\n");
        goto __ERROR;
    }

    // 从设备读取数据
    while ((ret = av_read_frame(fmt_ctx, pkt)) >= 0) {
        if(pkt->stream_index==videoindex){
            av_log(NULL, AV_LOG_INFO, "packet size is %d(%p)   \n", pkt->size,pkt->data);
            ret = avcodec_send_packet(dec_ctx, pkt);//发送packet到解码队列中
            if(ret < 0){
                printf("Decode Error.\n");
                return -1;
            }
            av_packet_unref(pkt);// 解除引用，防止不必要的内存占用
            while(avcodec_receive_frame(dec_ctx,pFrame)==0) { //循环获取AVFrame解码数据
                // AV_PIX_FMT_BGR0转成AV_PIX_FMT_YUV420P
                sws_scale(img_convert_ctx,(const uint8_t **)pFrame->data, pFrame->linesize, 0, dec_ctx->height,
                                                        pFrameYUV420->data, pFrameYUV420->linesize);
                pFrameYUV420->pts = base++;//加编号
                //编码frame获取packet，将AV_PIX_FMT_YUV420P编码成H264格式并推流；
                encode(enc_ctx, pFrameYUV420, newpkt, pusher);
            }
        }
        av_usleep(33000);//等待33ms
    }
    encode(enc_ctx, NULL, newpkt, pusher);//间接方法写入文件尾

    __ERROR:
    // 关闭设备并释放ctx
    if (fmt_ctx) {
        avformat_close_input(&fmt_ctx);
        avformat_free_context(fmt_ctx);
    }
    if(pFrame) av_frame_free(&pFrame);
    if(pFrameYUV420) av_frame_free(&pFrameYUV420);
    if(pkt) av_packet_free(&pkt);//释放AVPacket
    if(newpkt) av_packet_free(&newpkt);//释放AVPacket
    if(img_convert_ctx) sws_freeContext(img_convert_ctx);//释放img_convert_ctx
    if(pusher) delete pusher;//如果开辟了空间，记得释放

    av_log(NULL, AV_LOG_DEBUG, "finish!\n");
    return 0;
}
/**
 * 最简单的基于FFmpeg的AVDevice例子（读取摄像头）
 * https://blog.csdn.net/leixiaohua1020/article/details/39702113
 * 本程序实现了本地摄像头数据的获取解码和显示。是基于FFmpeg
 * 的libavdevice类库最简单的例子。通过该例子，可以学习FFmpeg中
 * libavdevice类库的使用方法。
 * 在Linux下可以使用video4linux2读取摄像头设备。
 */


#include <stdio.h>
#define __STDC_CONSTANT_MACROS
//Linux...
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavdevice/avdevice.h>
#include "SDL/SDL.h"  //依赖libSDL.so库

//自定义事件（SDL_USEREVENT+n），只要枚举值不超过0xFFFF即可
#define SFM_REFRESH_EVENT  (SDL_USEREVENT + 1)
#define SFM_BREAK_EVENT  (SDL_USEREVENT + 2)

//等于1时进程结束，退出进程
int thread_exit=0;

// 画面刷新线程
// 通常使用多线程的方式进行画面刷新管理，主线程进入主循环中等待事件，
// 画面刷新线程在一段时间后发送画面刷新事件，主线程收到画面刷新事件后进行画面刷新操作。
int sfp_refresh_thread(void *opaque)
{
	thread_exit=0;
	while (!thread_exit) {
		SDL_Event event;
		event.type = SFM_REFRESH_EVENT;
		//向队列中发送一个事件,标准的SDL 1.2队列的事件数上限数为 128，当队列已满时，新的事件将会被扔掉
		SDL_PushEvent(&event);
		SDL_Delay(100);//单位为ms,表示刷新间隔，不能太小，不然很容易满事件队列
	}
	thread_exit=0;
	//结束
	SDL_Event event;
	event.type = SFM_BREAK_EVENT;
	SDL_PushEvent(&event);
	return 0;
}


int main(int argc, char* argv[])
{
 
	AVFormatContext	*pFormatCtx;
	int				i, videoindex;
	AVCodecContext	*pCodecCtx;
	
	avdevice_register_all();//注册libavdevice
	pFormatCtx = avformat_alloc_context();
 
    //Linux下打开输入设备
	AVInputFormat *ifmt=(AVInputFormat *)av_find_input_format("video4linux2");
	if(avformat_open_input(&pFormatCtx,"/dev/video0",ifmt,NULL)!=0){
		printf("Couldn't open input stream.\n");
		return -1;
	}

	//检查是否从输入设备获取输入流
	if(avformat_find_stream_info(pFormatCtx,NULL)<0)
	{
		printf("Couldn't find stream information.\n");
		return -1;
	}
	videoindex=-1;
	for(i=0; i<pFormatCtx->nb_streams; i++){
		if(pFormatCtx->streams[i]->codecpar->codec_type==AVMEDIA_TYPE_VIDEO)
		{
			videoindex=i;
			break;
		}
	}
	if(videoindex==-1)
	{
		printf("Couldn't find a video stream.\n");
		return -1;
	}

	//创建AVCodecContext结构体
	pCodecCtx = avcodec_alloc_context3(NULL);
    if (pCodecCtx == NULL){  
		printf("Could not allocate AVCodecContext \n");  
		return -1;  
    }
	//将AVCodecParameters结构体中码流参数拷贝到AVCodecContext结构体
    avcodec_parameters_to_context(pCodecCtx, pFormatCtx->streams[videoindex]->codecpar); 
	//选择解码器
	AVCodec	*pCodec=(AVCodec *)avcodec_find_decoder(pCodecCtx->codec_id);
	if(pCodec==NULL)
	{
		printf("Codec not found.\n");
		return -1;
	}

	//利用pCodec初始化一个音视频编解码器pCodecCtx（AVCodecContext）
	if(avcodec_open2(pCodecCtx, pCodec,NULL)<0)
	{
		printf("Could not open codec.\n");
		return -1;
	}

	//定义AVFrame
	AVFrame	*pFrame,*pFrameYUV420;
	pFrame=av_frame_alloc();
	pFrameYUV420=av_frame_alloc();
	//SDL----------------------------
	if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {  //初始化SDL库
		printf( "Could not initialize SDL - %s\n", SDL_GetError()); 
		return -1;
	} 
	int screen_w=0,screen_h=0;
	SDL_Surface *screen; 
	screen_w = pCodecCtx->width;
	screen_h = pCodecCtx->height;
	screen = SDL_SetVideoMode(screen_w, screen_h, 0,0);//设置窗口模式

	if(!screen) {  
		printf("SDL: could not set video mode - exiting:%s\n",SDL_GetError());  
		return -1;
	}

	SDL_Overlay *bmp; 
	bmp = SDL_CreateYUVOverlay(pCodecCtx->width, pCodecCtx->height,SDL_YV12_OVERLAY, screen); 
	//三个参数表示创建的overlay将接收 YV12 类型的数据，应通过 FFmpeg 库将源视频数据转换为对应类型
	SDL_Rect rect;
	rect.x = 0;    
	rect.y = 0;    
	rect.w = screen_w;    
	rect.h = screen_h;  
	//SDL End------------------------


	int ret;
	AVPacket *packet=(AVPacket *)av_malloc(sizeof(AVPacket));

	struct SwsContext *img_convert_ctx;
	img_convert_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL); 
	//转换成AV_PIX_FMT_YUV420P格式
	//参数1：被转换源的宽、参数2：被转换源的高、参数3：被转换源的格式，eg：YUV、RGB等
	//参数4：转换后指定的宽、参数5：转换后指定的高、参数6：转换后指定的格式同参数3的格式、参数7：转换所使用的算法
	SDL_Thread *video_tid =SDL_CreateThread(sfp_refresh_thread,NULL);
    if(!video_tid){
		printf( "Could not Create SDL ThreadL - %s\n", SDL_GetError()); 
        return -1;
    }
	
	SDL_WM_SetCaption("Read Camera",NULL);//设置窗口的标题和ICON图标
	//事件
	SDL_Event event;
	for (;;) {
		SDL_WaitEvent(&event);//当事件队列为空时阻塞等待一个事件，并将CPU占用释放掉。
		// printf("event = %d \n",event.type);
		if(event.type==SFM_REFRESH_EVENT){
			//------------------------------
			if(av_read_frame(pFormatCtx, packet)>=0){
				if(packet->stream_index==videoindex){
					ret = avcodec_send_packet(pCodecCtx, packet);//发送packet到解码队列中
					if(ret < 0){
						printf("Decode Error.\n");
						return -1;
					}
					av_packet_unref(packet);// 解除引用，防止不必要的内存占用
					while(avcodec_receive_frame(pCodecCtx,pFrame)==0) { //循环获取AVFrame解码数据
						SDL_LockYUVOverlay(bmp);//对YUV加锁
						pFrameYUV420->data[0]=bmp->pixels[0];
						pFrameYUV420->data[1]=bmp->pixels[2];
						pFrameYUV420->data[2]=bmp->pixels[1];     
						pFrameYUV420->linesize[0]=bmp->pitches[0];
						pFrameYUV420->linesize[1]=bmp->pitches[2];   
						pFrameYUV420->linesize[2]=bmp->pitches[1];
						sws_scale(img_convert_ctx, (const unsigned char* const*)pFrame->data, pFrame->linesize, 0, pCodecCtx->height, pFrameYUV420->data, pFrameYUV420->linesize);
						//sws_scale将图像数据转换为 YUV420P，转换后的数据被塞入pFrameYUV422,pFrameYUV422在前面已经与overlay建立联系。
						SDL_UnlockYUVOverlay(bmp);//对YUV解锁
						SDL_DisplayYUVOverlay(bmp, &rect); //负责显示图片
						// rect 结构体指定播放区域的位置和缩放尺寸
					}
				}
			}else{
				thread_exit=1;
			}
		}else if(event.type==SDL_QUIT){
			// printf("%d \n",__LINE__);
			thread_exit=1;
		}else if(event.type==SFM_BREAK_EVENT){
			break;
		}
	}

	SDL_Quit();
	av_packet_free(&packet);//释放AVPacket
	sws_freeContext(img_convert_ctx);//释放img_convert_ctx
	av_frame_free(&pFrameYUV420);
	av_frame_free(&pFrame);
	avcodec_close(pCodecCtx);
	avformat_close_input(&pFormatCtx);
	return 0;
}
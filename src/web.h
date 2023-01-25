#ifndef __WEB_H__
#define __WEB_H__

void *web_thread(void *arg);

void web_submit_fft_main(uint8_t *_data, uint32_t _length);

#endif /* __WEB_H__ */
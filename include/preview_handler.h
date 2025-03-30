#ifndef PREVIEW_HANDLER_H
#define PREVIEW_HANDLER_H
#include <gst/gst.h>
class PreviewHandler {
public:
    PreviewHandler();
    ~PreviewHandler();
    bool setupPipeline();
private:
    GstElement* pipeline = nullptr;
    GstElement* src = nullptr;
    GstElement* convert = nullptr;
    GstElement* sink = nullptr;
};
#endif

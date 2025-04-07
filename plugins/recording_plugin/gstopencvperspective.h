#ifndef GST_OPENCV_PERSPECTIVE_H
#define GST_OPENCV_PERSPECTIVE_H

#include <gst/gst.h>
#include <gst/video/video.h>
#include <opencv2/opencv.hpp>
#include <vector>
#include <mutex>

G_BEGIN_DECLS

#define GST_TYPE_OPENCV_PERSPECTIVE (gst_opencv_perspective_get_type())
G_DECLARE_FINAL_TYPE(GstOpencvPerspective, gst_opencv_perspective, GST, OPENCV_PERSPECTIVE, GstVideoFilter)

// Ensure proper export declaration:
#ifdef __cplusplus
extern "C" {
#endif


#ifdef __cplusplus
}
#endif

struct _GstOpencvPerspective {
    GstVideoFilter base;
    
    // Properties
    gdouble p1_x, p1_y;
    gdouble p2_x, p2_y;
    gdouble p3_x, p3_y;
    gdouble p4_x, p4_y;
    
    // Internal state
    std::mutex mutex;
    cv::Mat transform_matrix;
    bool matrix_valid;
};

G_END_DECLS

#endif
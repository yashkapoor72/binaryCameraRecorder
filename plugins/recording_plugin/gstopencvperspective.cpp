#include "gstopencvperspective.h"
#include <opencv2/opencv.hpp>

GST_DEBUG_CATEGORY_STATIC(gst_opencv_perspective_debug);
#define GST_CAT_DEFAULT gst_opencv_perspective_debug

// Plugin metadata definitions
#define GST_PACKAGE_NAME "OpenCV Perspective Plugin"
#define GST_PACKAGE_ORIGIN "https://example.com"
#define PACKAGE "opencvperspective"
#define GST_PACKAGE_NAME "OpenCV Perspective Plugin"
#define PACKAGE_VERSION "1.0"
#define PACKAGE_STRING "OpenCV Perspective Plugin 1.0"
#define PACKAGE_TARNAME "opencvperspective"
#define GST_LICENSE "LGPL"

#define DEFAULT_P1_X 0.0
#define DEFAULT_P1_Y 0.0
#define DEFAULT_P2_X 1.0
#define DEFAULT_P2_Y 0.0
#define DEFAULT_P3_X 1.0
#define DEFAULT_P3_Y 1.0
#define DEFAULT_P4_X 0.0
#define DEFAULT_P4_Y 1.0

enum {
    PROP_0,
    PROP_P1_X,
    PROP_P1_Y,
    PROP_P2_X,
    PROP_P2_Y,
    PROP_P3_X,
    PROP_P3_Y,
    PROP_P4_X,
    PROP_P4_Y,
};

G_DEFINE_TYPE(GstOpencvPerspective, gst_opencv_perspective, GST_TYPE_VIDEO_FILTER)

static void gst_opencv_perspective_set_property(GObject *object, guint prop_id,
                                              const GValue *value, GParamSpec *pspec);
static void gst_opencv_perspective_get_property(GObject *object, guint prop_id,
                                              GValue *value, GParamSpec *pspec);
static GstFlowReturn gst_opencv_perspective_transform_frame_ip(GstVideoFilter *filter,
                                                              GstVideoFrame *frame);
static void update_transform_matrix(GstOpencvPerspective *filter);

static void gst_opencv_perspective_class_init(GstOpencvPerspectiveClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstVideoFilterClass *video_filter_class = GST_VIDEO_FILTER_CLASS(klass);
    
    gobject_class->set_property = gst_opencv_perspective_set_property;
    gobject_class->get_property = gst_opencv_perspective_get_property;
    
    video_filter_class->transform_frame_ip = gst_opencv_perspective_transform_frame_ip;

    gst_element_class_set_metadata(
        GST_ELEMENT_CLASS(klass),
        "OpenCV Perspective Transformer",
        "Filter/Effect/Video",
        "Applies a perspective transform using OpenCV", 
        "Yash Kapoor <kapooryash7202@gmail.com>" 
    );
    
    // Register properties for points (p1-x, p1-y, ..., p4-y)
    g_object_class_install_property(gobject_class, PROP_P1_X,
        g_param_spec_double("p1-x", "Point 1 X", "X coordinate of first point (0-1)",
                           0.0, 1.0, DEFAULT_P1_X,
                           (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    
    g_object_class_install_property(gobject_class, PROP_P1_Y,
        g_param_spec_double("p1-y", "Point 1 Y", "Y coordinate of first point (0-1)",
                           0.0, 1.0, DEFAULT_P1_Y,
                           (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    
    g_object_class_install_property(gobject_class, PROP_P2_X,
        g_param_spec_double("p2-x", "Point 2 X", "X coordinate of second point (0-1)",
                           0.0, 1.0, DEFAULT_P2_X,
                           (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    
    g_object_class_install_property(gobject_class, PROP_P2_Y,
        g_param_spec_double("p2-y", "Point 2 Y", "Y coordinate of second point (0-1)",
                           0.0, 1.0, DEFAULT_P2_Y,
                           (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    
    g_object_class_install_property(gobject_class, PROP_P3_X,
        g_param_spec_double("p3-x", "Point 3 X", "X coordinate of third point (0-1)",
                           0.0, 1.0, DEFAULT_P3_X,
                           (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    
    g_object_class_install_property(gobject_class, PROP_P3_Y,
        g_param_spec_double("p3-y", "Point 3 Y", "Y coordinate of third point (0-1)",
                           0.0, 1.0, DEFAULT_P3_Y,
                           (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    
    g_object_class_install_property(gobject_class, PROP_P4_X,
        g_param_spec_double("p4-x", "Point 4 X", "X coordinate of fourth point (0-1)",
                           0.0, 1.0, DEFAULT_P4_X,
                           (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    
    g_object_class_install_property(gobject_class, PROP_P4_Y,
        g_param_spec_double("p4-y", "Point 4 Y", "Y coordinate of fourth point (0-1)",
                           0.0, 1.0, DEFAULT_P4_Y,
                           (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
}

static void gst_opencv_perspective_init(GstOpencvPerspective *filter) {
    filter->p1_x = DEFAULT_P1_X;
    filter->p1_y = DEFAULT_P1_Y;
    filter->p2_x = DEFAULT_P2_X;
    filter->p2_y = DEFAULT_P2_Y;
    filter->p3_x = DEFAULT_P3_X;
    filter->p3_y = DEFAULT_P3_Y;
    filter->p4_x = DEFAULT_P4_X;
    filter->p4_y = DEFAULT_P4_Y;
    filter->matrix_valid = false;
}

static void update_transform_matrix(GstOpencvPerspective *filter) {
    std::lock_guard<std::mutex> lock(filter->mutex);
    
    std::vector<cv::Point2f> src_points = {
        cv::Point2f(filter->p1_x, filter->p1_y),
        cv::Point2f(filter->p2_x, filter->p2_y),
        cv::Point2f(filter->p3_x, filter->p3_y),
        cv::Point2f(filter->p4_x, filter->p4_y)
    };
    
    std::vector<cv::Point2f> dst_points = {
        cv::Point2f(0.0f, 0.0f),
        cv::Point2f(1.0f, 0.0f),
        cv::Point2f(1.0f, 1.0f),
        cv::Point2f(0.0f, 1.0f)
    };
    
    filter->transform_matrix = cv::getPerspectiveTransform(src_points, dst_points);
    filter->matrix_valid = true;
}

static void gst_opencv_perspective_set_property(GObject *object, guint prop_id,
                                              const GValue *value, GParamSpec *pspec) {
    GstOpencvPerspective *filter = GST_OPENCV_PERSPECTIVE(object);
    
    std::lock_guard<std::mutex> lock(filter->mutex);
    
    switch (prop_id) {
        case PROP_P1_X:
            filter->p1_x = g_value_get_double(value);
            break;
        case PROP_P1_Y:
            filter->p1_y = g_value_get_double(value);
            break;
        case PROP_P2_X:
            filter->p2_x = g_value_get_double(value);
            break;
        case PROP_P2_Y:
            filter->p2_y = g_value_get_double(value);
            break;
        case PROP_P3_X:
            filter->p3_x = g_value_get_double(value);
            break;
        case PROP_P3_Y:
            filter->p3_y = g_value_get_double(value);
            break;
        case PROP_P4_X:
            filter->p4_x = g_value_get_double(value);
            break;
        case PROP_P4_Y:
            filter->p4_y = g_value_get_double(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
    
    update_transform_matrix(filter);
}

static void gst_opencv_perspective_get_property(GObject *object, guint prop_id,
                                              GValue *value, GParamSpec *pspec) {
    GstOpencvPerspective *filter = GST_OPENCV_PERSPECTIVE(object);
    
    std::lock_guard<std::mutex> lock(filter->mutex);
    
    switch (prop_id) {
        case PROP_P1_X:
            g_value_set_double(value, filter->p1_x);
            break;
        case PROP_P1_Y:
            g_value_set_double(value, filter->p1_y);
            break;
        case PROP_P2_X:
            g_value_set_double(value, filter->p2_x);
            break;
        case PROP_P2_Y:
            g_value_set_double(value, filter->p2_y);
            break;
        case PROP_P3_X:
            g_value_set_double(value, filter->p3_x);
            break;
        case PROP_P3_Y:
            g_value_set_double(value, filter->p3_y);
            break;
        case PROP_P4_X:
            g_value_set_double(value, filter->p4_x);
            break;
        case PROP_P4_Y:
            g_value_set_double(value, filter->p4_y);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}

static GstFlowReturn gst_opencv_perspective_transform_frame_ip(GstVideoFilter *filter,
                                                              GstVideoFrame *frame) {
                     GstOpencvPerspective *opencv_filter = GST_OPENCV_PERSPECTIVE(filter);

    
    if (!opencv_filter->matrix_valid) {
        return GST_FLOW_OK;
    }
    
    std::lock_guard<std::mutex> lock(opencv_filter->mutex);
    
    cv::Mat img(frame->info.height, frame->info.width, CV_8UC3, 
               GST_VIDEO_FRAME_PLANE_DATA(frame, 0), 
               GST_VIDEO_FRAME_PLANE_STRIDE(frame, 0));
    
    cv::Mat warped;
    cv::warpPerspective(img, warped, opencv_filter->transform_matrix, img.size(),
                       cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar());
    
    warped.copyTo(img);
    
    return GST_FLOW_OK;
}


static gboolean plugin_init(GstPlugin *plugin) {
    // Initialize debug category FIRST
    GST_DEBUG_CATEGORY_INIT(gst_opencv_perspective_debug,
        "opencvperspective",
        0,
        "OpenCV Perspective Transform Element");
    
    g_print("Plugin initialization starting...\n");
    
    // Register the element
    if (!gst_element_register(plugin, "opencvperspective",
        GST_RANK_NONE, GST_TYPE_OPENCV_PERSPECTIVE)) {
        g_print("Plugin registration failed!\n");
        return FALSE;
    }
    
    g_print("Plugin registered successfully\n");
    return TRUE;
}

GST_PLUGIN_DEFINE(
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    opencvperspective,
    "OpenCV Perspective Transformer",
    plugin_init,
    "1.0.0",
    "LGPL",
    "OpenCV Perspective Plugin",
    "https://github.com/yashkapoor72"
)

#ifdef __cplusplus
extern "C" {
    #endif

    GST_EXPORT void opencvperspective_register(void) {
        static gboolean registered = FALSE;
        if (!registered) {
            GST_PLUGIN_STATIC_REGISTER(opencvperspective);
            registered = TRUE;
        }
    }

    #ifdef __cplusplus
}
#endif
bool GstRecording::takeScreenshot(const std::string& outputPathSs) {
    std::lock_guard<std::mutex> lock(mutex);
    
    if (recordings.empty()) {
        std::cerr << "No active recording found for screenshot" << std::endl;
        return false;
    }

    // Get the first recording session
    auto& session = recordings.begin()->second;
    
    if (!session.pipeline || !GST_IS_ELEMENT(session.pipeline)) {
        std::cerr << "Invalid pipeline for screenshot" << std::endl;
        return false;
    }

    // Create elements in NULL state
    GstElement* queue_screenshot = gst_element_factory_make("queue", "queue_screenshot");
    GstElement* jpegenc = gst_element_factory_make("jpegenc", "jpegenc");
    GstElement* filesink = gst_element_factory_make("filesink", "screenshot_filesink");
    
    if (!queue_screenshot || !jpegenc || !filesink) {
        std::cerr << "Failed to create screenshot elements" << std::endl;
        if (queue_screenshot) gst_object_unref(queue_screenshot);
        if (jpegenc) gst_object_unref(jpegenc);
        if (filesink) gst_object_unref(filesink);
        return false;
    }

    // Configure filesink
    g_object_set(filesink, 
        "location", outputPathSs.c_str(),
        "sync", FALSE,  // No need to sync for single frame
        NULL);

    // Configure jpegenc
    g_object_set(jpegenc,
        "quality", 85,
        NULL);

    // Add elements to pipeline before linking
    gst_bin_add_many(GST_BIN(session.pipeline), 
        queue_screenshot, jpegenc, filesink, 
        NULL);

    // Link elements
    if (!gst_element_link_many(queue_screenshot, jpegenc, filesink, NULL)) {
        std::cerr << "Failed to link screenshot elements" << std::endl;
        gst_bin_remove_many(GST_BIN(session.pipeline), 
            queue_screenshot, jpegenc, filesink, 
            NULL);
        return false;
    }

    // Get the tee element
    GstElement* tee = gst_bin_get_by_name(GST_BIN(session.pipeline), "screenshot_tee");
    if (!tee) {
        std::cerr << "Failed to find tee element in pipeline" << std::endl;
        gst_bin_remove_many(GST_BIN(session.pipeline), 
            queue_screenshot, jpegenc, filesink, 
            NULL);
        return false;
    }

    // Request a new src pad from the tee
    GstPad* tee_src_pad = gst_element_get_request_pad(tee, "src_%u");
    GstPad* queue_sink_pad = gst_element_get_static_pad(queue_screenshot, "sink");
    
    if (!tee_src_pad || !queue_sink_pad) {
        std::cerr << "Failed to get pads for screenshot branch" << std::endl;
        if (tee_src_pad) gst_object_unref(tee_src_pad);
        if (queue_sink_pad) gst_object_unref(queue_sink_pad);
        gst_object_unref(tee);
        gst_bin_remove_many(GST_BIN(session.pipeline), 
            queue_screenshot, jpegenc, filesink, 
            NULL);
        return false;
    }

    // Link tee to queue
    if (gst_pad_link(tee_src_pad, queue_sink_pad) != GST_PAD_LINK_OK) {
        std::cerr << "Failed to link tee to screenshot branch" << std::endl;
        gst_object_unref(tee_src_pad);
        gst_object_unref(queue_sink_pad);
        gst_object_unref(tee);
        gst_bin_remove_many(GST_BIN(session.pipeline), 
            queue_screenshot, jpegenc, filesink, 
            NULL);
        return false;
    }

    // Set up a probe to remove the screenshot branch after capturing one frame
    GstPad* jpegenc_sink_pad = gst_element_get_static_pad(jpegenc, "sink");
    if (jpegenc_sink_pad) {
        gulong probe_id = gst_pad_add_probe(jpegenc_sink_pad, 
            GST_PAD_PROBE_TYPE_BUFFER,
            [](GstPad* pad, GstPadProbeInfo* info, gpointer user_data) -> GstPadProbeReturn {
                GstElement* pipeline = static_cast<GstElement*>(user_data);
                
                // Post a message to the bus to remove elements in main thread context
                gst_element_post_message(pipeline,
                    gst_message_new_application(GST_OBJECT(pipeline),
                        gst_structure_new("remove-screenshot-elements",
                            "queue", G_TYPE_STRING, "queue_screenshot",
                            "jpegenc", G_TYPE_STRING, "jpegenc",
                            "filesink", G_TYPE_STRING, "screenshot_filesink",
                            NULL)));
                
                return GST_PAD_PROBE_REMOVE;
            }, session.pipeline, NULL);
        
        gst_object_unref(jpegenc_sink_pad);
    }

    // Sync states - this must be done after linking
    gst_element_sync_state_with_parent(queue_screenshot);
    gst_element_sync_state_with_parent(jpegenc);
    gst_element_sync_state_with_parent(filesink);

    // Clean up references
    gst_object_unref(tee_src_pad);
    gst_object_unref(queue_sink_pad);
    gst_object_unref(tee);

    return true;
}
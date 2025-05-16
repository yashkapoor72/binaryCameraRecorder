import sys

import cv2


def click_event(event, x, y, flags, param):
    if event == cv2.EVENT_LBUTTONDOWN:
        print(f"[{x}, {y}],")
        cv2.circle(frame, (x, y), 5, (255, 0, 0), -1)
        cv2.imshow("Video", frame)


def print_usage(args):
    print(args[0], "-i CAMERA_INDEX -h camera_height -w camera_width")


if len(sys.argv) < 7:
    print_usage(sys.argv)
    exit(1)

height = None
width = None
index = None
for i in range(3):
    match sys.argv[1 + i * 2]:
        case "-i":
            index = int(sys.argv[(i + 1) * 2])
        case "-h":
            height = int(sys.argv[(i + 1) * 2])
        case "-w":
            width = int(sys.argv[(i + 1) * 2])

assert height is not None
assert width is not None
assert index is not None

cap = cv2.VideoCapture(index)
cap.set(cv2.CAP_PROP_FRAME_WIDTH, width)
cap.set(cv2.CAP_PROP_FRAME_HEIGHT, height)

if not cap.isOpened():
    print("Error: Camera could not be opened.")
    exit()

cv2.namedWindow("Video", cv2.WINDOW_NORMAL)
cv2.setMouseCallback("Video", click_event)


# Capture video
while True:
    ret, frame = cap.read()
    if not ret:
        print("Failed to grab frame")
        break

    cv2.imshow("Video", frame)

    # Break the loop when 'q' is pressed
    if cv2.waitKey(1) & 0xFF == ord("q"):
        break

# Release the camera and close all windows
cap.release()
cv2.destroyAllWindows()

if __name__ == "__main__":
    pass
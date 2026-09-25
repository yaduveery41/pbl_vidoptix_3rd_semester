#include <opencv2/opencv.hpp>
#include <iostream>
#include <string>

using namespace std;

int main() {

    string videoPath;

    cout << "Enter video path: ";
    cin >> videoPath;

    cv::VideoCapture video(videoPath);

    if (!video.isOpened()) {
        cerr << "Could not open video.\n";
        return 1;
    }

    cv::Mat frame, grayFrame;
    int choice;

    do {
        cout << "\n========== MENU ==========\n";
        cout << "1. Read first frame\n";
        cout << "2. Convert to grayscale\n";
        cout << "3. Save grayscale frame\n";
        cout << "4. Display frame dimensions\n";
        cout << "5. Exit\n";
        cout << "Enter your choice: ";
        cin >> choice;

        switch (choice) {

            case 1:
                video.set(cv::CAP_PROP_POS_FRAMES, 0);

                if (video.read(frame))
                    cout << "First frame read successfully.\n";
                else
                    cout << "Could not read frame.\n";

                break;

            case 2:
                video.set(cv::CAP_PROP_POS_FRAMES, 0);
                video.read(frame);

                if (!frame.empty()) {
                    cv::cvtColor(frame, grayFrame, cv::COLOR_BGR2GRAY);
                    cout << "Frame converted to grayscale.\n";
                } else {
                    cout << "Could not read frame.\n";
                }

                break;

            case 3:
                if (!grayFrame.empty()) {

                    string outputPath;

                    cout << "Enter output image path: ";
                    cin >> outputPath;

                    if (cv::imwrite(outputPath, grayFrame))
                        cout << "Grayscale frame saved successfully.\n";
                    else
                        cout << "Could not save frame.\n";

                } else {
                    cout << "First convert the frame to grayscale (Option 2).\n";
                }

                break;

            case 4:
                if (!grayFrame.empty()) {
                    cout << "Width  : " << grayFrame.cols << endl;
                    cout << "Height : " << grayFrame.rows << endl;
                } else {
                    cout << "Convert the frame to grayscale first.\n";
                }

                break;

            case 5:
                cout << "Exiting program...\n";
                break;

            default:
                cout << "Invalid choice!\n";
        }

    } while (choice != 5);

    return 0;
}

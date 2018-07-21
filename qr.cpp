/*
* Nick Bild
* nick.bild@gmail.com
* 2018-03-19
* Version 0.2
* Extract QR codes from real-time video stream.
* Decode QR codes and display relevant information
* in video frame.
*/

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/highgui.hpp>
#include <zbar.h> 
#include <sqlite3.h>

#include <iostream>
#include <math.h>
#include <string.h>
#include <ctime>
#include <map>

using namespace cv;
using namespace std;
using namespace zbar;

int cameraID = 0; 		// Camera device ID.
string employeeID = "12345";	// This headset is linked to an employee.

// Information linked to QR code last seen.
string qr_info = "";
string qr_type = "";

// Database variables.
sqlite3 *db;

// Keep a record of what this provider has seen and when.
static void visionMemory(string qr_data) {
        sqlite3_stmt *stmt;

	// Get unix timestamp.
	long int dateTimeInt = std::time(0);
	std::ostringstream dateTimeSS;
	dateTimeSS << dateTimeInt;
	string dateTime = dateTimeSS.str();

        // Insert data.
        string sql = "INSERT INTO view_field (employee_id, qr_data, type, timestamp) VALUES ('" + employeeID + "', '" + qr_data + "', '" + qr_type + "', '" + dateTime + "');";
        sqlite3_prepare(db, sql.c_str(), -1, &stmt, NULL);
        sqlite3_step(stmt);

        // Clean up.
	sqlite3_finalize(stmt);

}

// Translate QR code into associated information stored in database.
static string translateQR(string qr_data) {
	sqlite3_stmt *stmt;

	// Query database.
	string sqlStr = "SELECT info, type FROM qr_lookup WHERE qr_data='" + qr_data + "';";
	const char* sql = sqlStr.c_str();

	int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);

	if (rc != SQLITE_OK) {
		cout << "SQL ERROR!";
	}

        rc = sqlite3_step(stmt);

	if (rc == SQLITE_ROW) {
		qr_info = (const char *)sqlite3_column_text(stmt, 0);
		qr_type = (const char *)sqlite3_column_text(stmt, 1);
	}

	sqlite3_finalize(stmt);

	// Remember what this user has just seen.
	visionMemory(qr_data);

	// Return results.
	return qr_info;

}

// Check for any alerts.
static string checkAlerts(string qr_data) {
	string alertText = "";

	// Get unix timestamp.
        long int dateTimeInt = std::time(0);
	dateTimeInt -= 15; // 10 seconds ago.
        std::ostringstream dateTimeSS;
        dateTimeSS << dateTimeInt;
        string dateTime = dateTimeSS.str();

	// If user is looking at a patient...
	if (qr_type == "patient") {
		// Are any drugs currently on this patient's drug administration schedule?
		sqlite3_stmt *stmt;
		string sqlStr = "SELECT drug_id FROM drug_admin_schedule WHERE patient_id='" + qr_data + "';";
		const char* sql = sqlStr.c_str();

		int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);

		// Loop through results and save drug IDs in hash map.
		std::map<std::string, int> patient_drugs;
		while (sqlite3_step(stmt) == SQLITE_ROW) {
			string drug_id = (const char *)sqlite3_column_text(stmt, 0);
			patient_drugs[drug_id] = 1;
		}

		sqlite3_finalize(stmt);

		// Has this user looked at any drugs in the last X seconds?
		sqlStr = "SELECT qr_data FROM view_field WHERE employee_id='" + employeeID + "' and type='drug' and timestamp >" + dateTime + ";";
		sql = sqlStr.c_str();

		rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);

		while (sqlite3_step(stmt) == SQLITE_ROW) {
			string drug_id = (const char *)sqlite3_column_text(stmt, 0);
			if (patient_drugs[drug_id] != 1) {
				alertText = "Warning! Drug has not been prescribed to this patient.";
			}
		}

		sqlite3_finalize(stmt);
	}

	return alertText;
}

// Capture video frames and display information on top of frames.
int main(int argc, char** argv) {
	// Open database connection.
        int rc = sqlite3_open("qrdb.db", &db);

        if (rc) {
                fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
                return -1;
        }
	
	VideoCapture stream1(cameraID); // 0 is the ID of device 0.

	// Was video device initialized?
        if (!stream1.isOpened()) {
                cout << "Error opening camera.";
        }

	Mat image;
	string qr_data = "";
	string qr_data_last = "";
	string dspText = "";
	string alertText = "";

	// Display text parameters.
	cv::Scalar colorNormal = cv::Scalar(244, 69, 66);
	cv::Scalar colorAlert = cv::Scalar(0, 0, 255);
        string delimiter = "|||";
	int frame_cnt = 0;

	// Inifinte loop.
        while (true) {
                stream1.read(image); // Grab a frame from the camera.

		// Create a zbar reader.
    		ImageScanner scanner;

		// Convert frame to grayscale.
		Mat grayscale;
		cvtColor(image, grayscale, CV_BGR2GRAY);

	        // Get image data.
	        int width = grayscale.cols;
	        int height = grayscale.rows;
	        uchar *raw = (uchar *)(grayscale.data);

	        // Wrap image data.
	        Image image_zbar(width, height, "Y800", raw, width * height);

	        // Scan the image for barcodes.
	        int n = scanner.scan(image_zbar);

		// Iterate through detected barcodes.
		for (Image::SymbolIterator symbol = image_zbar.symbol_begin(); symbol != image_zbar.symbol_end(); ++symbol) {
			qr_data = symbol->get_data();
		}

		// If a new QR code has been found, display updated information.
		if (qr_data_last != qr_data) {
			// Translate QR data into information for display.
			dspText = translateQR(qr_data);

			// Check for any alerts.
			alertText = checkAlerts(qr_data);
		}

		// Write display text to frame.
		int dspTextY = 30;
		size_t pos = 0;
		string token;
		string dspTextTemp = dspText; // Temporary version of dspText that is modified as frame is written.

		// Write to display line by line.
		while ((pos = dspTextTemp.find(delimiter)) != std::string::npos) {
			token = dspTextTemp.substr(0, pos);
			putText(image, token, cvPoint(15, dspTextY), FONT_HERSHEY_COMPLEX_SMALL, 0.8, colorNormal, 1, CV_AA);
    			dspTextTemp.erase(0, pos + delimiter.length());
			dspTextY += 15;
		}
		putText(image, dspTextTemp, cvPoint(15, dspTextY), FONT_HERSHEY_COMPLEX_SMALL, 0.8, colorNormal, 1, CV_AA);

		// Display any alerts.
		if (alertText != "") {
			frame_cnt++;
			if (frame_cnt % 10 != 0) {
				putText(image, alertText, cvPoint(40, 440), FONT_HERSHEY_COMPLEX_SMALL, 0.8, colorAlert, 1, CV_AA);
			} else {
				putText(image, alertText, cvPoint(40, 440), FONT_HERSHEY_COMPLEX_SMALL, 0.8, colorNormal, 1, CV_AA);
			}
		}

		// Remember the QR code detected this time.
		qr_data_last = qr_data;

		// Display camera frame in window.
                imshow("Doc InSight", image);

                // Display current frame for X ms.
                // Exit loop on any key press.
                if (waitKey(5) >= 0) {
                        break;
                }
	}

	// Clean up and exit.
	sqlite3_close(db);
	return 0;
}


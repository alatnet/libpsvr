#include "DataViewer.h"
#include "psvr.h"

#include "glm/glm.hpp"
#include "glm/gtc/quaternion.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/euler_angles.hpp"

#include "BMI055Integrator.h"

//---------------------------------------------
//Sensor Thread
//---------------------------------------------
SensorThread::SensorThread(struct psvr_context *ctx) {
	this->ctx = ctx;
	this->running = true;
	this->updateSpd = 2;

	this->paused = false;
}

SensorThread::~SensorThread() {
}

void SensorThread::run() {
	if (!ctx) return; //dont have a context, dont run.
	psvr_sensor_frame frame;

	while (running) {
		psvr_read_sensor_sync(ctx, (uint8_t *)&frame, sizeof(psvr_sensor_frame));
		emit frameUpdate(&frame);

		switch (this->updateSpd) {
		default:
			yieldCurrentThread();
			break;
		case 1:
			msleep(500); //0.5 seconds
			break;
		case 2:
			msleep(1000); //1 second
			break;
		case 3:
			msleep(2000); //2 seconds
			break;
		case 4:
			msleep(3000); //3 seconds
			break;
		}
	}

	this->exit(0);
}

void SensorThread::setUpdateSpeed(int spd) { this->updateSpd = spd; }
void SensorThread::stopThread() { this->running = false; }
/*void SensorThread::pauseData() {
	if (this->paused)
		this->pauseMutex->unlock();
	else
		this->pauseMutex->lock();

	this->paused = !this->paused;
}*/
//---------------------------------------------


//---------------------------------------------
//Control Thread
//---------------------------------------------
ControlThread::ControlThread(struct psvr_context *ctx) {
	this->ctx = ctx;
	this->setTerminationEnabled(true);
}

void ControlThread::run() {
	if (!ctx) return; //dont have a context, dont run.
	psvr_control_frame frame;
	//memset(&frame, 0, sizeof(psvr_control_frame));

	forever {
		/*psvr_read_control_sync(ctx, (uint8_t*)&frame, sizeof(psvr_control_frame_header));

		usleep(10 * 1000);

		if (frame.s.header.magic == 0xAA && frame.s.header.length <= 0x3C) {
			psvr_read_control_sync(
				ctx,
				frame.s.data,
				frame.s.header.length
			);

			emit frameUpdate(&frame);
		}
		memset(&frame, 0, sizeof(psvr_control_frame));*/

		psvr_read_control_sync(ctx, (uint8_t*)&frame, sizeof(psvr_control_frame));
		emit frameUpdate(&frame);
	}

	this->exit(0);
} 
void ControlThread::stopThread() {
	this->terminate();
}
//---------------------------------------------

//---------------------------------------------
//Data Viewer Window
//---------------------------------------------
DataViewer::DataViewer(QWidget *parent)
	: QMainWindow(parent)
{
	ui.setupUi(this);

	ui.txt_control_log->append("Initializing.");

	ui.cmbx_update_spd->setCurrentIndex(2);

	DataViewer::loggingArea = ui.txt_control_log;

	psvr_set_log((psvr_log*)DataViewer::psvr_logger);

	psvr_context *ctx = nullptr;
	this->ctx = nullptr;

	int err = psvr_open(&ctx); //open psvr context

	this->sensorThread = nullptr;
	this->controlThread = nullptr;

	if (err == 0) {
		this->ctx = ctx;

		ui.txt_control_log->append("Connection Established.");

		this->sensorThread = new SensorThread(this->ctx);
		connect(
			this->ui.cmbx_update_spd,
			SIGNAL(currentIndexChanged(int)),
			this->sensorThread,
			SLOT(setUpdateSpeed(int))
		);
		connect(
			this->sensorThread,
			SIGNAL(frameUpdate(void*)),
			this,
			SLOT(sensorFrame(void*)),
			Qt::BlockingQueuedConnection
		);

		this->sensorThread->start();

		this->controlThread = new ControlThread(this->ctx);
		connect(
			this->controlThread,
			SIGNAL(frameUpdate(void*)),
			this,
			SLOT(controlFrame(void*)),
			Qt::BlockingQueuedConnection
		);

		this->controlThread->start();
	} else
		ui.txt_control_log->append("Error establishing connection.");

	//setup buttons
	connect(
		this->ui.btn_headsetOn,
		SIGNAL(clicked()),
		this,
		SLOT(headsetOn())
	);
	connect(
		this->ui.btn_headsetOff,
		SIGNAL(clicked()),
		this,
		SLOT(headsetOff())
	);
	connect(
		this->ui.btn_vrMode,
		SIGNAL(clicked()),
		this,
		SLOT(enableVRMode())
	);
	connect(
		this->ui.btn_vrMode2,
		SIGNAL(clicked()),
		this,
		SLOT(enableVRMode2())
	);
	connect(
		this->ui.btn_getDeviceInfo,
		SIGNAL(clicked()),
		this,
		SLOT(getDeviceInfo())
	);
	connect(
		this->ui.btn_processorOff,
		SIGNAL(clicked()),
		this,
		SLOT(turnProcessorOff())
	);
	connect(
		this->ui.btn_processorClear,
		SIGNAL(clicked()),
		this,
		SLOT(clearProcesorBuffer())
	);

	connect(
		this->ui.btn_send_manual,
		SIGNAL(clicked()),
		this,
		SLOT(sendManualCommand())
	);

	connect(
		this->ui.btn_recalibrate,
		SIGNAL(clicked()),
		this,
		SLOT(recalibrate())
	);
	connect(
		this->ui.btn_recenter,
		SIGNAL(clicked()),
		this,
		SLOT(recenter())
	);

	BMI055Integrator::Init(BMI055Integrator::AScale::AFS_2G, BMI055Integrator::Gscale::GFS_2000DPS);
}

DataViewer::~DataViewer() {
	psvr_set_log(nullptr);

	if (this->sensorThread) {
		this->sensorThread->stopThread();
		this->sensorThread->wait();

		disconnect(
			this->ui.cmbx_update_spd,
			SIGNAL(currentIndexChanged(int)),
			this->sensorThread,
			SLOT(setUpdateSpeed(int))
		);
		disconnect(
			this->sensorThread,
			SIGNAL(frameUpdate(void*)),
			this,
			SLOT(sensorFrame(void*))
		);

		delete this->sensorThread;
		this->sensorThread = nullptr;
	}

	if (this->controlThread) {
		this->controlThread->stopThread();
		this->controlThread->wait();

		disconnect(
			this->controlThread,
			SIGNAL(frameUpdate(void*)),
			this,
			SLOT(controlFrame(void*))
		);

		delete this->controlThread;
		this->controlThread = nullptr;
	}

	if (this->ctx) psvr_close(this->ctx); //close the context
}

QString hexToString(uint8_t x) {
	return QString("0x%1").arg(x, 2, 16, QLatin1Char('0'));
}
QString hexToString(uint16_t x) {
	return QString("0x%1").arg(x, 4, 16, QLatin1Char('0'));
}
QString hexToString(uint32_t x) {
	return QString("0x%1").arg(x, 8, 16, QLatin1Char('0'));
}

const glm::quat fixQuat(glm::quat quat) {
	//cant figure out the math to swap x and y axis in quaternion, doing with euler angles
	auto tmp = glm::eulerAngles(quat);
	return glm::normalize(glm::quat_cast(glm::eulerAngleYXZ(-tmp.x, -tmp.y, -tmp.z)));
}

void DataViewer::sensorFrame(void *data) {
	psvr_sensor_frame* frame = (psvr_sensor_frame*)data;

	{ //sensors
		ui.lbl_s2_btn_reserved->setText(frame->s.button.reserved ? "True" : "False");
		ui.lbl_s2_btn_plus->setText(frame->s.button.plus ? "True" : "False");
		ui.lbl_s2_btn_minus->setText(frame->s.button.minus ? "True" : "False");
		ui.lbl_s2_btn_mute->setText(frame->s.button.mute ? "True" : "False");

		ui.lbl_s2_reserved0->setText(hexToString(frame->s.reserved0));
		ui.lbl_s2_volume->setText(QString::number(frame->s.volume));

		QString reserved1;
		for (int i = 0; i < 5; i++) {
			reserved1.append(hexToString(frame->s.reserved1[i]));
			reserved1.append(" ");
		}
		ui.lbl_s2_reserved1->setText(reserved1);

		ui.lbl_s2_status_worn->setText(frame->s.status.worn ? "True" : "False");
		ui.lbl_s2_status_display->setText(frame->s.status.display_active ? "False" : "True");
		ui.lbl_s2_status_hdmi->setText(frame->s.status.hdmi_disconnected ? "True" : "False");
		ui.lbl_s2_status_micMute->setText(frame->s.status.microphone_muted ? "True" : "False");
		ui.lbl_s2_status_headphone->setText(frame->s.status.headphone_connected ? "True" : "False");
		ui.lbl_s2_status_reserved->setText(frame->s.status.reserved ? "True" : "False");
		ui.lbl_s2_status_tick->setText(frame->s.status.tick ? "True" : "False");

		QString reserved2;
		for (int i = 0; i < 7; i++) {
			reserved2.append(hexToString(frame->s.reserved2[i]));
			reserved2.append(" ");
		}
		ui.lbl_s2_reserved2->setText(reserved2);

		ui.lbl_s2_data0_timestamp->setText(QString::number(frame->s.data[0].timestamp));
		ui.lbl_s2_data0_gyro_yaw->setText(QString::number(frame->s.data[0].gyro.yaw));
		ui.lbl_s2_data0_gyro_pitch->setText(QString::number(frame->s.data[0].gyro.pitch));
		ui.lbl_s2_data0_gyro_roll->setText(QString::number(frame->s.data[0].gyro.roll));
		ui.lbl_s2_data0_accel_x->setText(QString::number(frame->s.data[0].accel.x));
		ui.lbl_s2_data0_accel_y->setText(QString::number(frame->s.data[0].accel.y));
		ui.lbl_s2_data0_accel_z->setText(QString::number(frame->s.data[0].accel.z));

		ui.lbl_s2_data1_timestamp->setText(QString::number(frame->s.data[1].timestamp));
		ui.lbl_s2_data1_gyro_yaw->setText(QString::number(frame->s.data[1].gyro.yaw));
		ui.lbl_s2_data1_gyro_pitch->setText(QString::number(frame->s.data[1].gyro.pitch));
		ui.lbl_s2_data1_gyro_roll->setText(QString::number(frame->s.data[1].gyro.roll));
		ui.lbl_s2_data1_accel_x->setText(QString::number(frame->s.data[1].accel.x));
		ui.lbl_s2_data1_accel_y->setText(QString::number(frame->s.data[1].accel.y));
		ui.lbl_s2_data1_accel_z->setText(QString::number(frame->s.data[1].accel.z));

		ui.lbl_s2_calstatus->setText(QString::number(frame->s.calStatus));
		ui.lbl_s2_ready->setText(QString::number(frame->s.ready));

		QString reserved3;
		for (int i = 0; i < 3; i++) {
			reserved3.append(hexToString(frame->s.reserved3[i]));
			reserved3.append(" ");
		}
		ui.lbl_s2_reserved3->setText(reserved3);

		ui.lbl_s2_voltVal->setText(QString::number(frame->s.voltageVal));
		ui.lbl_s2_voltRef->setText(QString::number(frame->s.voltageRef));
		ui.lbl_s2_irSensor->setText(QString::number(frame->s.irSensor));

		QString reserved4;
		for (int i = 0; i < 5; i++) {
			reserved4.append(hexToString(frame->s.reserved4[i]));
			reserved4.append(" ");
		}
		ui.lbl_s2_reserved4->setText(reserved4);

		ui.lbl_s2_pakSeq->setText(QString::number(frame->s.packetSeq));

		QString raw;
		for (int i = 0; i < 64; i++) {
			raw.append(hexToString(frame->raw[i]));
			raw.append(" ");
		}
		ui.lbl_sensor_raw->setText(raw);
	}

	{ //sensors 2
		glm::quat quat = fixQuat(BMI055Integrator::Parse((void *)frame));
		auto euler = BMI055Integrator::ToEuler(&quat);

		ui.lbl_s2_pitch->setText(QString::number(glm::degrees(euler.x)));
		ui.lbl_s2_yaw->setText(QString::number(glm::degrees(euler.y)));
		ui.lbl_s2_roll->setText(QString::number(glm::degrees(euler.z)));

		ui.lbl_s2_qw->setText(QString::number(glm::degrees(quat.w)));
		ui.lbl_s2_qx->setText(QString::number(glm::degrees(quat.x)));
		ui.lbl_s2_qy->setText(QString::number(glm::degrees(quat.y)));
		ui.lbl_s2_qz->setText(QString::number(glm::degrees(quat.z)));

		ui.lbl_s2_status->setText(BMI055Integrator::calibrating ? "Calibrating" : "OK");
	}
}

void DataViewer::controlFrame(void* data) {
	psvr_control_frame* frame = (psvr_control_frame*)data;

	//--------------------
	//control frame info
	//--------------------
	ui.lbl_control_id->setText(hexToString(frame->s.header.r_id));
	ui.lbl_control_status->setText(hexToString(frame->s.header.gp_id));
	ui.lbl_control_start->setText(hexToString(frame->s.header.magic));
	ui.lbl_control_len->setText(hexToString(frame->s.header.length));

	QString craw;
	for (int i = 0; i < 68; i++) {
		craw.append(hexToString(frame->raw[i]));
		craw.append(" ");
	}
	ui.lbl_control_raw->setText(craw);
	//--------------------

	switch (frame->s.header.r_id) {
	case eRT_Info:
	{
		ui.lbl_info_reserved0->setText(hexToString(frame->s.dinfo.s.reserved0));
		ui.lbl_info_major->setText(QString::number(frame->s.dinfo.s.version.major));
		ui.lbl_info_minor->setText(QString::number(frame->s.dinfo.s.version.minor));

		ui.lbl_info_reserved1->setText(hexToString(frame->s.dinfo.s.reserved1));
		ui.lbl_info_reserved2->setText(hexToString(frame->s.dinfo.s.reserved2));
		ui.lbl_info_reserved3->setText(hexToString(frame->s.dinfo.s.reserved3));

		ui.lbl_info_serial->setText(QString::fromLatin1((const char*)frame->s.dinfo.s.serialNumber, 16));


		QString reserved4;
		for (int i = 0; i < 5; i++) {
			reserved4.append(hexToString(frame->s.dinfo.s.reserved4[i]));
			reserved4.append(" ");
		}
		ui.lbl_info_reserved4->setText(reserved4);

		QString raw;
		for (int i = 0; i < 48; i++) {
			raw.append(hexToString(frame->s.dinfo.raw[i]));
			raw.append(" ");
		}
		ui.lbl_info_raw->setText(raw);
	}
		break;
	case eRT_Status:
	{
		ui.lbl_cStatus_headsetOn->setText(frame->s.dstatus.s.headsetOn ? "True" : "False");
		ui.lbl_cStatus_worn->setText(frame->s.dstatus.s.worn ? "True" : "False");
		ui.lbl_cStatus_cinematic->setText(frame->s.dstatus.s.cinematic ? "True" : "False");
		ui.lbl_cStatus_mReserved0->setText(frame->s.dstatus.s.maskreserved0 ? "True" : "False");
		ui.lbl_cStatus_headphones->setText(frame->s.dstatus.s.headphones ? "True" : "False");
		ui.lbl_cStatus_mute->setText(frame->s.dstatus.s.mute ? "True" : "False");
		ui.lbl_cStatus_cec->setText(frame->s.dstatus.s.cec ? "True" : "False");
		ui.lbl_cStatus_mReserved1->setText(frame->s.dstatus.s.maskreserved1 ? "True" : "False");
		ui.lbl_cStatus_vol->setText(QString::number(frame->s.dstatus.s.volume));
		ui.lbl_cStatus_reserved0->setText(hexToString(frame->s.dstatus.s.reserved0));
		ui.lbl_cStatus_bridgeOutputID->setText(hexToString(frame->s.dstatus.s.bridgeOutputID));

		QString raw;
		for (int i = 0; i < 8; i++) {
			raw.append(hexToString(frame->s.dstatus.raw[i]));
			raw.append(" ");
		}
		ui.lbl_cStatus_raw->setText(raw);
	}
		break;
	case eRT_Unsolicited:
	{
		ui.lbl_report_id->setText(hexToString(frame->s.ureport.s.id));
		ui.lbl_report_code->setText(hexToString((uint8_t)frame->s.ureport.s.code));

		QString msg;
		for (int i = 0; i < 58; i++) {
			msg.append((const char)frame->s.ureport.s.message[i]);
			if (frame->s.ureport.s.message[i] == '\0') break;
		}
		ui.lbl_report_msg->setText(msg);
		ui.txt_control_log->append(msg);

		QString raw;
		for (int i = 0; i < 60; i++) {
			raw.append(hexToString(frame->s.ureport.raw[i]));
			raw.append(" ");
		}
		ui.lbl_report_raw->setText(raw);
	}
		break;
	}
}

//---------------------------------------------
//Button functions
//---------------------------------------------
void DataViewer::headsetOn() {
	if (!ctx) return; //dont have a context, dont run.
	ui.txt_control_log->append("Turning headset on.");
	uint32_t on = 1;
	psvr_send_command_sync(ctx, eRID_HeadsetPower, (uint8_t*)&on, 4);
}
void DataViewer::headsetOff() {
	if (!ctx) return; //dont have a context, dont run.
	ui.txt_control_log->append("Turning headset off.");
	uint32_t on = 0;
	psvr_send_command_sync(ctx, eRID_HeadsetPower, (uint8_t*)&on, 4);
}
void DataViewer::enableVRMode() { //true vr mode
	if (!ctx) return; //dont have a context, dont run.
	ui.txt_control_log->append("Enabling VR Mode.");
	uint8_t data[] = { 0x01, 0x00, 0x00, 0x00 };
	psvr_send_command_sync(ctx, eRID_SetMode, data, 4);
}
void DataViewer::enableVRMode2() { //cinematic mode
	if (!ctx) return; //dont have a context, dont run.
	ui.txt_control_log->append("Enabling Cinematic Mode.");
	uint8_t data[] = { 0x00, 0x00, 0x00, 0x00 };
	psvr_send_command_sync(ctx, eRID_SetMode, data, 4);
}
/*void DataViewer::enableCinematicMode() {
	if (!ctx) return; //dont have a context, dont run.
	ui.txt_control_log->append("Enabling Cinematic Mode.<br />");
}*/
void DataViewer::getDeviceInfo() {
	if (!ctx) return; //dont have a context, dont run.
	ui.txt_control_log->append("Getting Device Info.");
	uint8_t cmd[] = { 0x80, 0, 0, 0, 0, 0, 0, 0 };
	psvr_send_command_sync(ctx, eRID_DeviceInfo, cmd, 8);
}
void DataViewer::turnProcessorOff() {
	if (!ctx) return; //dont have a context, dont run.
	ui.txt_control_log->append("Turning Processor Off.");
	uint8_t on[] = { 0x01, 0x00, 0x00, 0x00 };
	psvr_send_command_sync(ctx, eRID_ProcessorPower, on, 4);

	/*if (this->ctx) {
		ui.txt_control_log->append("Disconnecting.");
		if (this->sensorThread) {
			this->sensorThread->stopThread();

			disconnect(
				this->ui.cmbx_update_spd,
				SIGNAL(currentIndexChanged(int)),
				this->sensorThread,
				SLOT(setUpdateSpeed(int))
			);
			disconnect(
				this->sensorThread,
				SIGNAL(frameUpdate(void*)),
				this,
				SLOT(sensorFrame(void*))
			);

			delete this->sensorThread;
			this->sensorThread = nullptr;
		}

		if (this->controlThread) {
			this->controlThread->stopThread();

			disconnect(
				this->controlThread,
				SIGNAL(frameUpdate(void*)),
				this,
				SLOT(controlFrame(void*))
			);

			delete this->controlThread;
			this->controlThread = nullptr;
		}

		psvr_close(this->ctx);
		this->ctx = nullptr;
	}*/
}
void DataViewer::clearProcesorBuffer() {
	if (!ctx) return; //dont have a context, dont run.
	ui.txt_control_log->append("Clearing buffer?");
	uint8_t data[] = { 0x00, 0x00, 0x00, 0x00 };
	psvr_send_command_sync(ctx, eRID_ProcessorPower, data, 4);
}

void DataViewer::sendManualCommand() {
	if (!ctx) return; //dont have a context, dont run.
	
	QString reportIDStr = ui.txt_control_reportID->text();
	QString dataStr = ui.txt_control_data->text();

	if (reportIDStr.size() == 0 || dataStr.size() == 0) {
		ui.txt_control_log->append("Empty Report ID or Data.");
		return;
	}

	//if we are in a hex format (i.e. 0xFF) remove the 0x
	if (reportIDStr.at(1) == 'x' || reportIDStr.at(1) == 'X') reportIDStr = reportIDStr.mid(2);

	QByteArray reportID = QByteArray::fromHex(reportIDStr.toLatin1());

	if (reportID.size() == 1) {
		//if we are in a hex format (i.e. 0xFF) remove the 0x
		if (dataStr.at(1) == 'x' || dataStr.at(1) == 'X') dataStr = dataStr.mid(2);

		QByteArray data = QByteArray::fromHex(dataStr.toLatin1());

		ui.txt_control_log->append("Sending command.");
		psvr_send_command_sync(
			ctx,
			(uint8_t)reportID.data()[0],
			(uint8_t*)data.data(),
			(uint32_t)data.size()
		);
	} else {
		ui.txt_control_log->append("Report ID is not one byte!");
	}

	/*
	QByteArray array1;
	array1.resize(2);
	array1[0] = 0xFF;
	array1[1] = 0xFF;

	QString str = "0xFFFF";
	QString value =  str.mid(2);    // "FFFF"   <- just the hex values!
	QByteArray array2 = QByteArray::fromHex(value.toLatin1());

	qDebug() << array1;             // not printable chars
	qDebug() << array2;

	qDebug() << array1.toHex();     // to have a printable form use "toHex()"!
	qDebug() << array2.toHex();
	*/
}

void DataViewer::recenter() {
	BMI055Integrator::Recenter();
}

void DataViewer::recalibrate() {
	BMI055Integrator::Recalibrate();
}
//---------------------------------------------

//---------------------------------------------
//Logging stuff
//---------------------------------------------
QTextEdit* DataViewer::loggingArea = nullptr;

void DataViewer::psvr_logger(const char * msg, va_list args) {
	char buff[256];
	vsnprintf(buff, 256, msg, args);

	DataViewer::loggingArea->append(buff);
}
//---------------------------------------------
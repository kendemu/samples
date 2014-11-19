﻿/*!
* ==========================================================================================
*  @brief  transmit quatenions of each joints of Kinect_V2　SDK to SIGServer.
*  @file   KinectApp.cpps
*  @date   2014/11/14
*  @author National Institute of Informatics
*  @par    1.0.0
* ==========================================================================================
*/
//---------------------------------------------------------------------------
// Includes
//---------------------------------------------------------------------------
#include "KinectApp.h"
#include "ComPtr.h"

xn::Context g_Context;
xn::ScriptNode g_scriptNode;
xn::DepthGenerator g_DepthGenerator;
xn::UserGenerator g_UserGenerator;
xn::Player g_Player;

XnBool g_bNeedPose = FALSE;
XnChar g_strPose[20] = "";
XnBool g_bDrawBackground = TRUE;
XnBool g_bDrawPixels = TRUE;
XnBool g_bDrawSkeleton = TRUE;
XnBool g_bPrintID = TRUE;
XnBool g_bPrintState = TRUE;

#define ERROR_CHECK( ret )  \
    if ( (ret) != S_OK ) {    \
        std::stringstream ss;	\
        ss << "failed " #ret " " << std::hex << ret << std::endl;			\
        throw std::runtime_error( ss.str().c_str() );			\
		    }


#define LOG( message ) { Logger::Write( message ); }


/*!
* @brief positions of left shoulder and left elbow in grasping
*/
XnSkeletonJointPosition ssld, selb, shand;

enum SigVec {
	HIP = 0,
	HTOTOR,
	WAIST,
	RSHOULDER,
	LSHOULDER,
	RELBOW,
	LELBOW,
	LEG
};


/*!
* @brief define prefixes of left and right hands
*/
char strLabel[50] = "";
XnUserID aUsers[15];
XnUInt16 nUsers = 15;
#ifndef _REVERSE_HAND
const char *GRASP_ARM[2] = { "LARM", "RARM" };
const char *GRASP_LEG[2] = { "LLEG", "RLEG" };
#else
const char *GRASP_ARM[2] = { "RARM", "LARM" };
const char *GRASP_LEG[2] = { "RLEG", "LLEG" };
#endif


//values of joint angles in SIGVerse
XnPoint3D KinectApp::GetSigVec(int sigvec)
{
	XnPoint3D p;
	if (sigvec == WAIST || sigvec == HTOTOR)
	{
		p.X = 0;
		p.Y = 1;
		p.Z = 0;
	}
	else if (sigvec == RSHOULDER || sigvec == RELBOW)
	{
		p.X = -1;
		p.Y = 0;
		p.Z = 0;
	}
	else if (sigvec == HIP || sigvec == LSHOULDER || sigvec == LELBOW)
	{
		p.X = 1;
		p.Y = 0;
		p.Z = 0;
	}
	else if (sigvec == LEG)
	{
		p.X = 0;
		p.Y = -1;
		p.Z = 0;
	}
	return p;
}

void KinectApp::initialize()
{
	// get default Kinect
	ERROR_CHECK(::GetDefaultKinectSensor(&kinect));

	// open the Kinect
	ERROR_CHECK(kinect->Open());

	BOOLEAN isOpen = false;
	ERROR_CHECK(kinect->get_IsOpen(&isOpen));
	if (!isOpen){
		throw std::runtime_error("Kinect not opened");
	}

	//get color reader
	ComPtr<IColorFrameSource> colorFrameSource;
	ERROR_CHECK(kinect->get_ColorFrameSource(&colorFrameSource));
	ERROR_CHECK(colorFrameSource->OpenReader(&colorFrameReader));

	//get  color image size
	ComPtr<IFrameDescription> colorFrameDescription;
	ERROR_CHECK(colorFrameSource->CreateFrameDescription(
	ColorImageFormat::ColorImageFormat_Bgra, &colorFrameDescription));
	ERROR_CHECK(colorFrameDescription->get_Width(&colorWidth));
	ERROR_CHECK(colorFrameDescription->get_Height(&colorHeight));
	ERROR_CHECK(colorFrameDescription->get_BytesPerPixel(&colorBytesPerPixel));

	//create buffer
	colorBuffer.resize(colorWidth * colorHeight * colorBytesPerPixel);

	//get body reader
	ComPtr<IBodyFrameSource> bodyFrameSource;
	ERROR_CHECK(kinect->get_BodyFrameSource(&bodyFrameSource));
	ERROR_CHECK(bodyFrameSource->OpenReader(&bodyFrameReader));

	log->Initialize("log.txt");
}

void KinectApp::run()
{
	while (true) {
		update();
		draw();

		auto key = cv::waitKey(10);
		if (key == 'q'){
			break;
		}
	}
}

//update data
void KinectApp::update()
{
	updateColorFrame();
	//updateInfrared();
	updateBodyFrame();
}

//udpate body frame
void KinectApp::updateBodyFrame()
{
	//get frame
	ComPtr<IBodyFrame> bodyFrame;
	auto ret = bodyFrameReader->AcquireLatestFrame(&bodyFrame);
	if (ret == S_OK){
		//get data
		ERROR_CHECK(bodyFrame->GetAndRefreshBodyData(6, &bodies[0]));
		//release frames if you do not use smart pointer.     
		//bodyFrame->Release();
	}
}



void KinectApp::draw()
{
	drawBodyIndexFrame();
}

/*!
* ------------------------------------------------------------------------------------------
* @brief （R×P×Q：backward rotation(forward in SIGVerse)）return 3D vector calculated from rotation quaternion and 3D vector
* @param[in/out] XnPoint3D& input and output source 3D vector
* @param[in]     Quaterion& input rotation quaternion(parent bone:initial line, child bone:radius vector)
* ------------------------------------------------------------------------------------------
*/
void KinectApp::RotVec(XnPoint3D &v, Quaternion q)
{
	double rx = (double)(v.X *  q.qw + v.Y * -q.qz + v.Z *  q.qy);
	double ry = (double)(v.X *  q.qz + v.Y *  q.qw + v.Z * -q.qx);
	double rz = (double)(v.X * -q.qy + v.Y *  q.qx + v.Z *  q.qw);
	double rw = (double)(v.X *  q.qx + v.Y *  q.qy + v.Z *  q.qz);

	v.X = (XnFloat)(q.qx *  rw + q.qy *  rz + q.qz * -ry + q.qw * rx);
	v.Y = (XnFloat)(q.qx * -rz + q.qy *  rw + q.qz *  rx + q.qw * ry);
	v.Z = (XnFloat)(q.qx *  ry + q.qy * -rx + q.qz *  rw + q.qw * rz);
}

/*!
* ------------------------------------------------------------------------------------------
* @brief (R×P×Q：backward rotation(forward in SIGVerse)）return 3D vector calculated from rotation quaternion and 3D vector
* @param[in/out] XnPoint3D& input and output source 3D vector
* @param[in]     Quaterion& input rotation quaternion (parent bone:initial line, child bone:radius vector)
* @param[in]     bool       whether only X axis is valid or not
* ------------------------------------------------------------------------------------------
*/
void KinectApp::RotVec(XnPoint3D &v, Quaternion q, bool axis1) 
{
	//invalidate Y and Z axis if only X axis is valid 
	if (axis1 == true) {
		q.qy = 0.0;
	}

	// （R×P） calculate cross product of vector and quaternion
	float rw = v.X *  q.qx + v.Y *  q.qy + v.Z *  q.qz;
	float rx = v.X *  q.qw + v.Y * -q.qz + v.Z *  q.qy;
	float ry = v.X *  q.qz + v.Y *  q.qw + v.Z * -q.qx;
	float rz = v.X * -q.qy + v.Y *  q.qx + v.Z *  q.qw;

	// （P×Q）calculate the vector rotated by the quaternion
 	v.X = q.qx *  rw + q.qy *  rz + q.qz * -ry + q.qw * rx;
	v.Y = q.qx * -rz + q.qy *  rw + q.qz *  rx + q.qw * ry;
	v.Z = q.qx *  ry + q.qy * -rx + q.qz *  rw + q.qw * rz;

}


//update color frame
void KinectApp::updateColorFrame()
{
	//printf("updateColorFrame\n");
	//get frame
	ComPtr<IColorFrame> colorFrame;
	auto ret = colorFrameReader->AcquireLatestFrame(&colorFrame);
	if (ret == S_OK){
		//get data as BGRA format
		ERROR_CHECK(colorFrame->CopyConvertedFrameDataToArray(
			colorBuffer.size(), &colorBuffer[0], ColorImageFormat::ColorImageFormat_Bgra));
	}
}

/**
void KinectApp::updateInfrared()
{
printf("updateInfrared\n");
ComPtr<IInfraredFrame> infraredFrame;
auto ret = infraredFrameReader->AcquireLatestFrame(&infraredFrame);
if (ret == S_OK){
ERROR_CHECK(infraredFrame->CopyFrameDataToArray(infraredBuffer.size(), &infraredBuffer[0]));
// infraredFrame->Release();
}
}
*/

void KinectApp::drawBodyIndexFrame()
{
	//display joint coordinates in Depth coordinate system.
	//take care of inversion between height and width
	//1920,1080 > 512,484(1024,968)
	cv::Mat colorImage(colorHeight, colorWidth, CV_8UC4, &colorBuffer[0]);
	cv::Mat bodyImage(width, height, CV_8UC4);
	cv::Mat cut_img(colorImage, cv::Rect(448, 56, 1472, 912));
	//cv::Mat cut_img(colorImage, cv::Rect(0, 0, 1024, 968));
	//cv::Mat cut_img(colorImage, cv::Rect(896, 112, 1920, 1080));
	cv::resize(colorImage, bodyImage, cv::Size(width, height), 0, 0, 1);

	for (auto body : bodies){
		if (body == nullptr){
			continue;
		}

		BOOLEAN isTracked = false;
		ERROR_CHECK(body->get_IsTracked(&isTracked));
		if (!isTracked) {
			continue;
		}

		//JointOrientation jointOrientations[JointType_Count];
		system("cls");
		body->GetJointOrientations(JointType_Count, jointorientations);
		//showJointOrientations("SpineBase",jointorientations[0]);
		//showJointOrientations("SpineMid", jointorientations[1]);
		//showJointOrientations("Neck", jointorientations[2]);
		//showJointOrientations("Head", jointorientations[3]);

		//showJointOrientations("ElbowLeft", jointorientations[5]);
		//showJointOrientations("ElbowRight", jointorientations[9]);
		//showJointOrientations("WristLeft", jointorientations[6]);
		//showJointOrientations("WristRight", jointorientations[10]);
		//showJointOrientations("HandLeft", jointorientations[7]);
		//showJointOrientations("HandRight", jointorientations[11]);
		//showJointOrientations("HipLeft", jointorientations[12]);
		//showJointOrientations("HipRight", jointorientations[16]);

		//showJointOrientations("KneeLeft", jointorientations[13]);
		//showJointOrientations("KneeRight", jointorientations[17]);
		//showJointOrientations("AnkleLeft", jointorientations[14]);
		//showJointOrientations("AnkleRight", jointorientations[18]);
		//showJointOrientations("FootLeft", jointorientations[15]);
		//showJointOrientations("FootRight", jointorientations[19]);
		//showJointOrientations("SpineShoulder", jointorientations[20]);
		//showJointOrientations("ThumbLeft", jointorientations[22]);
		//showJointOrientations("ThumbRight", jointorientations[24]);
		//showJointOrientations("HandTipLeft", jointorientations[21]);
		//showJointOrientations("HandTipRight", jointorientations[23]);
		//
		//showJointOrientations("ShoulderLeft", jointorientations[4]);
		//showJointOrientations("ShoulderRight", jointorientations[8]);




		body->GetJoints(JointType::JointType_Count, joints);
		drawLine(bodyImage, joints[3], joints[2], cv::Scalar(0, 0, 200));
		drawLine(bodyImage, joints[2], joints[20], cv::Scalar(0, 0, 200));
		drawLine(bodyImage, joints[20], joints[8], cv::Scalar(0, 0, 200));
		drawLine(bodyImage, joints[20], joints[4], cv::Scalar(0, 0, 200));
		drawLine(bodyImage, joints[20], joints[1], cv::Scalar(0, 0, 200));
		drawLine(bodyImage, joints[8], joints[9], cv::Scalar(0, 0, 200));
		drawLine(bodyImage, joints[4], joints[5], cv::Scalar(0, 0, 200));
		drawLine(bodyImage, joints[9], joints[10], cv::Scalar(0, 0, 200));
		drawLine(bodyImage, joints[5], joints[6], cv::Scalar(0, 0, 200));
		drawLine(bodyImage, joints[10], joints[11], cv::Scalar(0, 0, 200));
		drawLine(bodyImage, joints[6], joints[7], cv::Scalar(0, 0, 200));
		drawLine(bodyImage, joints[11], joints[23], cv::Scalar(0, 0, 200));
		drawLine(bodyImage, joints[7], joints[21], cv::Scalar(0, 0, 200));
		drawLine(bodyImage, joints[10], joints[24], cv::Scalar(0, 0, 200));
		drawLine(bodyImage, joints[6], joints[22], cv::Scalar(0, 0, 200));
		drawLine(bodyImage, joints[1], joints[0], cv::Scalar(0, 0, 200));
		drawLine(bodyImage, joints[0], joints[16], cv::Scalar(0, 200, 0));//
		drawLine(bodyImage, joints[0], joints[12], cv::Scalar(0, 200, 0));//
		drawLine(bodyImage, joints[16], joints[17], cv::Scalar(0, 0, 200));
		drawLine(bodyImage, joints[12], joints[13], cv::Scalar(0, 0, 200));
		drawLine(bodyImage, joints[17], joints[18], cv::Scalar(0, 0, 200));
		drawLine(bodyImage, joints[13], joints[14], cv::Scalar(0, 0, 200));
		drawLine(bodyImage, joints[18], joints[19], cv::Scalar(0, 0, 200));
		drawLine(bodyImage, joints[14], joints[15], cv::Scalar(0, 0, 200));
		//showAllQuaternions("joints",joints);
		for (auto joint : joints)
		{
			//tracking joint
			if (joint.TrackingState == TrackingState::TrackingState_Tracked) {
				if (joint.JointType == JointType_HipRight)
				{
					drawEllipse(bodyImage, joint, 10, cv::Scalar(0, 255, 0));
				}
				else if (joint.JointType == JointType_HipLeft)
				{
					drawEllipse(bodyImage, joint, 10, cv::Scalar(255, 0, 0));
				}
				else
				{
					drawEllipse(bodyImage, joint, 10, cv::Scalar(255, 0, 0));
				}

				//when tracking left hand
				if (joint.JointType == JointType::JointType_HandLeft) {
					HandState handState;
					TrackingConfidence handConfidence;
					body->get_HandLeftState(&handState);
					body->get_HandLeftConfidence(&handConfidence);

					drawHandState(bodyImage, joint, handConfidence, handState);
				}
				//when tracking right hand
				else if (joint.JointType == JointType::JointType_HandRight) {
					HandState handState;
					TrackingConfidence handConfidence;
					body->get_HandRightState(&handState);
					body->get_HandRightConfidence(&handConfidence);

					drawHandState(bodyImage, joint, handConfidence, handState);
				}
			}
			//guess status
			else if (joint.TrackingState == TrackingState::TrackingState_Inferred) {
				drawEllipse(bodyImage, joint, 10, cv::Scalar(255, 255, 0));
			}
		}

	}
	cv::flip(bodyImage, bodyImage, 1); //flip horizontal
	cv::putText(bodyImage, message, cv::Point((int)50, (int)50), CV_FONT_HERSHEY_SIMPLEX, 0.2, cv::Scalar(0, 0, 200), 1, CV_AA);
	drawString(bodyImage, joints[0], 10, cv::Scalar(255, 255, 0));
	drawString(bodyImage, joints[1], 10, cv::Scalar(255, 255, 0));
	drawString(bodyImage, joints[2], 10, cv::Scalar(255, 255, 0));
	drawString(bodyImage, joints[3], 10, cv::Scalar(255, 255, 0));
	drawString(bodyImage, joints[4], 10, cv::Scalar(255, 255, 0));
	drawString(bodyImage, joints[5], 10, cv::Scalar(255, 255, 0));
	drawString(bodyImage, joints[6], 10, cv::Scalar(255, 255, 0));
	drawString(bodyImage, joints[7], 10, cv::Scalar(255, 255, 0));
	drawString(bodyImage, joints[8], 10, cv::Scalar(255, 255, 0));
	drawString(bodyImage, joints[9], 10, cv::Scalar(255, 255, 0));
	drawString(bodyImage, joints[10], 10, cv::Scalar(255, 255, 0));
	drawString(bodyImage, joints[11], 10, cv::Scalar(255, 255, 0));
	drawString(bodyImage, joints[12], 10, cv::Scalar(255, 255, 0));
	drawString(bodyImage, joints[13], 10, cv::Scalar(255, 255, 0));
	drawString(bodyImage, joints[14], 10, cv::Scalar(255, 255, 0));
	drawString(bodyImage, joints[15], 10, cv::Scalar(255, 255, 0));
	drawString(bodyImage, joints[16], 10, cv::Scalar(255, 255, 0));
	drawString(bodyImage, joints[17], 10, cv::Scalar(255, 255, 0));
	drawString(bodyImage, joints[18], 10, cv::Scalar(255, 255, 0));
	drawString(bodyImage, joints[19], 10, cv::Scalar(255, 255, 0));
	drawString(bodyImage, joints[20], 10, cv::Scalar(255, 255, 0));
	drawString(bodyImage, joints[21], 10, cv::Scalar(255, 255, 0));
	drawString(bodyImage, joints[22], 10, cv::Scalar(255, 255, 0));
	drawString(bodyImage, joints[23], 10, cv::Scalar(255, 255, 0));
	drawString(bodyImage, joints[24], 10, cv::Scalar(255, 255, 0));
	cv::imshow("Body Image", bodyImage);
}

void KinectApp::sendMessage(sigverse::SIGService *srv, std::string& agent_name)
{
	static bool bInitialized = false;
	//static GLuint depthTexID;
	static unsigned char* pDepthTexBuf;
	//static int texWidth, texHeight;

	//added for sigverse
	static char saddr[128];
	static int port;
	static int speed;
	static double move_speed;
	static int cnt;
	static bool start;
	static XnPoint3D startpos;



	//float topLeftX;
	//float topLeftY;
	//float bottomRightY;
	//float bottomRightX;
	//float texXpos;
	//float texYpos;

	if (!bInitialized)
	{
		start = false;

		TCHAR spd[8];
		// ƒf[ƒ^‘—MƒŒ[ƒg
		//printf("start of datafile\n");
		GetPrivateProfileString(_T("SETTING"), _T("SEND_SPEED"), '\0', spd, 256, _T("./SIGNiUserTracker.ini"));
		if (spd[0] == '\0') {
			speed = 1;
		}
		else {
			speed = atoi((char*)spd);
		}

		// ƒG[ƒWƒFƒ“ƒgˆÚ“®‘¬“x
		GetPrivateProfileString(_T("SETTING"), _T("MOVE_SPEED"), '\0', spd, 256, _T("./SIGNiUserTracker.ini"));
		if (spd[0] == '\0') {
			move_speed = 0.1;
		}
		else {
			move_speed = atof((char*)spd);
		}
		//printf("end of datafile\n");
		//texWidth = getClosestPowerOfTwo(dmd.XRes());
		//texHeight = getClosestPowerOfTwo(dmd.YRes());

		//printf("Initializing depth texture: width = %d, height = %d\n", texWidth, texHeight);
		//depthTexID = initTexture((void**)&pDepthTexBuf, texWidth, texHeight);

		//printf("Initialized depth texture: width = %d, height = %d\n", texWidth, texHeight);
		bInitialized = true;

		//topLeftX = dmd.XRes();
		//topLeftY = 0;
		//bottomRightY = dmd.YRes();
		//bottomRightX = 0;
		//texXpos = (float)dmd.XRes() / texWidth;
		//texYpos = (float)dmd.YRes() / texHeight;

		//memset(texcoords, 0, 8 * sizeof(float));
		//texcoords[0] = texXpos, texcoords[1] = texYpos, texcoords[2] = texXpos, texcoords[7] = texYpos;
	}

	unsigned int nValue = 0;
	unsigned int nHistValue = 0;
	unsigned int nIndex = 0;
	unsigned int nX = 0;
	unsigned int nY = 0;
	unsigned int nNumberOfPoints = 0;
	//XnUInt16 g_nXRes = dmd.XRes();
	//XnUInt16 g_nYRes = dmd.YRes();

	unsigned char* pDestImage = pDepthTexBuf;

	//const XnDepthPixel* pDepth = dmd.Data();
	//const XnLabel* pLabels = smd.Data();

	// Calculate the accumulative histogram
	//memset(g_pDepthHist, 0, MAX_DEPTH*sizeof(float));
	//for (nY = 0; nY<g_nYRes; nY++)
	//{
	//	for (nX = 0; nX<g_nXRes; nX++)
	//	{
	//		nValue = *pDepth;

	//		if (nValue != 0)
	//		{
	//			g_pDepthHist[nValue]++;
	//			nNumberOfPoints++;
	//		}

	//		pDepth++;
	//	}
	//}

	//for (nIndex = 1; nIndex<MAX_DEPTH; nIndex++)
	//{
	//	g_pDepthHist[nIndex] += g_pDepthHist[nIndex - 1];
	//}
	//if (nNumberOfPoints)
	//{
	//	for (nIndex = 1; nIndex<MAX_DEPTH; nIndex++)
	//	{
	//		g_pDepthHist[nIndex] = (unsigned int)(256 * (1.0f - (g_pDepthHist[nIndex] / nNumberOfPoints)));
	//	}
	//}

	//pDepth = dmd.Data();
	//if (g_bDrawPixels)
	//{
	//	XnUInt32 nIndex = 0;
	//	// Prepare the texture map
	//	for (nY = 0; nY<g_nYRes; nY++)
	//	{
	//		for (nX = 0; nX < g_nXRes; nX++, nIndex++)
	//		{

	//			pDestImage[0] = 0;
	//			pDestImage[1] = 0;
	//			pDestImage[2] = 0;
	//			if (g_bDrawBackground || *pLabels != 0)
	//			{
	//				nValue = *pDepth;
	//				XnLabel label = *pLabels;
	//				XnUInt32 nColorID = label % nColors;
	//				if (label == 0)
	//				{
	//					nColorID = nColors;
	//				}

	//				if (nValue != 0)
	//				{
	//					nHistValue = g_pDepthHist[nValue];

	//					pDestImage[0] = nHistValue * Colors[nColorID][0];
	//					pDestImage[1] = nHistValue * Colors[nColorID][1];
	//					pDestImage[2] = nHistValue * Colors[nColorID][2];
	//				}
	//			}

	//			pDepth++;
	//			pLabels++;
	//			pDestImage += 3;
	//		}

	//		pDestImage += (texWidth - g_nXRes) * 3;
	//	}
	//}
	//else
	//{
	//	xnOSMemSet(pDepthTexBuf, 0, 3 * 2 * g_nXRes*g_nYRes);
	//}

	//glBindTexture(GL_TEXTURE_2D, depthTexID);
	//glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, texWidth, texHeight, 0, GL_RGB, GL_UNSIGNED_BYTE, pDepthTexBuf);

	//// Display the OpenGL texture map
	//glColor4f(0.75, 0.75, 0.75, 1);

	//glEnable(GL_TEXTURE_2D);
	//DrawTexture(dmd.XRes(), dmd.YRes(), 0, 0);
	//glDisable(GL_TEXTURE_2D);

	//char strLabel[50] = "";
	//XnUserID aUsers[15];
	//XnUInt16 nUsers = 15;
	//g_UserGenerator.GetUsers(aUsers, nUsers);

#ifdef _KINECT_PLAY
	nUsers = 1;
#endif

	// ƒT[ƒo‚©‚ç‚ÌŽóM‚ðŠm”F‚µ‚Ü‚·
	srv->checkRecvData(10);
	int nUsers = 1;
	for (int i = 0; i < nUsers; ++i){
#ifndef _KINECT_PLAY
//#ifndef USE_GLES
//		if (g_bPrintID)
//		{
//
//			XnPoint3D com;
//			g_UserGenerator.GetCoM(aUsers[i], com);
//			g_DepthGenerator.ConvertRealWorldToProjective(1, &com, &com);
//
//			xnOSMemSet(strLabel, 0, sizeof(strLabel));
//			if (!g_bPrintState)
//			{
//				// Tracking
//				sprintf(strLabel, "%d", aUsers[i]);
//			}
//			else if (g_UserGenerator.GetSkeletonCap().IsTracking(aUsers[i]))
//			{
//				// Tracking
//				sprintf(strLabel, "%d - Tracking", aUsers[i]);
//			}
//			else if (g_UserGenerator.GetSkeletonCap().IsCalibrating(aUsers[i]))
//			{
//				// Calibrating
//				sprintf(strLabel, "%d - Calibrating [%s]", aUsers[i], GetCalibrationErrorString(m_Errors[aUsers[i]].first));
//			}
//			else
//			{
//				// Nothing
//				sprintf(strLabel, "%d - Looking for pose [%s]", aUsers[i], GetPoseErrorString(m_Errors[aUsers[i]].second));
//			}
//
//			glColor4f(1 - Colors[i%nColors][0], 1 - Colors[i%nColors][1], 1 - Colors[i%nColors][2], 1);
//
//			glRasterPos2i(com.X, com.Y);
//			glPrintString(GLUT_BITMAP_HELVETICA_18, strLabel);
//		}
//#endif
#endif

#ifndef _KINECT_PLAY
		//if (g_bDrawSkeleton && g_UserGenerator.GetSkeletonCap().IsTracking(aUsers[i])){
		if (true){
#endif

//#ifndef _KINECT_PLAY
//#ifndef USE_GLES
//			glBegin(GL_LINES);
//#endif
//
//			glColor4f(1 - Colors[aUsers[i] % nColors][0], 1 - Colors[aUsers[i] % nColors][1], 1 - Colors[aUsers[i] % nColors][2], 1);
//			DrawLimb(aUsers[i], XN_SKEL_HEAD, XN_SKEL_NECK);
//
//			DrawLimb(aUsers[i], XN_SKEL_NECK, XN_SKEL_LEFT_SHOULDER);
//			DrawLimb(aUsers[i], XN_SKEL_LEFT_SHOULDER, XN_SKEL_LEFT_ELBOW);
//			DrawLimb(aUsers[i], XN_SKEL_LEFT_ELBOW, XN_SKEL_LEFT_HAND);
//
//			DrawLimb(aUsers[i], XN_SKEL_NECK, XN_SKEL_RIGHT_SHOULDER);
//			DrawLimb(aUsers[i], XN_SKEL_RIGHT_SHOULDER, XN_SKEL_RIGHT_ELBOW);
//			DrawLimb(aUsers[i], XN_SKEL_RIGHT_ELBOW, XN_SKEL_RIGHT_HAND);
//
//			DrawLimb(aUsers[i], XN_SKEL_LEFT_SHOULDER, XN_SKEL_TORSO);
//			DrawLimb(aUsers[i], XN_SKEL_RIGHT_SHOULDER, XN_SKEL_TORSO);
//
//			DrawLimb(aUsers[i], XN_SKEL_TORSO, XN_SKEL_LEFT_HIP);
//			DrawLimb(aUsers[i], XN_SKEL_LEFT_HIP, XN_SKEL_LEFT_KNEE);
//			DrawLimb(aUsers[i], XN_SKEL_LEFT_KNEE, XN_SKEL_LEFT_FOOT);
//
//			DrawLimb(aUsers[i], XN_SKEL_TORSO, XN_SKEL_RIGHT_HIP);
//			DrawLimb(aUsers[i], XN_SKEL_RIGHT_HIP, XN_SKEL_RIGHT_KNEE);
//			DrawLimb(aUsers[i], XN_SKEL_RIGHT_KNEE, XN_SKEL_RIGHT_FOOT);
//
//			DrawLimb(aUsers[i], XN_SKEL_LEFT_HIP, XN_SKEL_RIGHT_HIP);
//#endif

			if (i == 0) {
				if (cnt < 0) {
					cnt = speed;
				}
				if (cnt == 0){
					//XnSkeletonJointPosition torso, lhip, rhip, neck, lshoul, lelb, lhand;
					//XnSkeletonJointPosition rshoul, relb, lknee, rknee, rhand, lfoot, rfoot, head;
					//g_UserGenerator.GetSkeletonCap().GetSkeletonJointPosition(aUsers[0], XN_SKEL_TORSO, torso);
					//g_UserGenerator.GetSkeletonCap().GetSkeletonJointPosition(aUsers[0], XN_SKEL_LEFT_HIP, lhip);
					//g_UserGenerator.GetSkeletonCap().GetSkeletonJointPosition(aUsers[0], XN_SKEL_RIGHT_HIP, rhip);
					//g_UserGenerator.GetSkeletonCap().GetSkeletonJointPosition(aUsers[0], XN_SKEL_NECK, neck);
					//g_UserGenerator.GetSkeletonCap().GetSkeletonJointPosition(aUsers[0], XN_SKEL_HEAD, head);
					//g_UserGenerator.GetSkeletonCap().GetSkeletonJointPosition(aUsers[0], XN_SKEL_LEFT_SHOULDER, lshoul);
					//g_UserGenerator.GetSkeletonCap().GetSkeletonJointPosition(aUsers[0], XN_SKEL_RIGHT_SHOULDER, rshoul);
					//g_UserGenerator.GetSkeletonCap().GetSkeletonJointPosition(aUsers[0], XN_SKEL_LEFT_ELBOW, lelb);
					//g_UserGenerator.GetSkeletonCap().GetSkeletonJointPosition(aUsers[0], XN_SKEL_RIGHT_ELBOW, relb);
					//g_UserGenerator.GetSkeletonCap().GetSkeletonJointPosition(aUsers[0], XN_SKEL_LEFT_HAND, lhand);
					//g_UserGenerator.GetSkeletonCap().GetSkeletonJointPosition(aUsers[0], XN_SKEL_RIGHT_HAND, rhand);
					//g_UserGenerator.GetSkeletonCap().GetSkeletonJointPosition(aUsers[0], XN_SKEL_LEFT_KNEE, lknee);
					//g_UserGenerator.GetSkeletonCap().GetSkeletonJointPosition(aUsers[0], XN_SKEL_RIGHT_KNEE, rknee);
					//g_UserGenerator.GetSkeletonCap().GetSkeletonJointPosition(aUsers[0], XN_SKEL_LEFT_FOOT, lfoot);
					//g_UserGenerator.GetSkeletonCap().GetSkeletonJointPosition(aUsers[0], XN_SKEL_RIGHT_FOOT, rfoot);

					XnSkeletonJointPosition SpineBase, torso, lhip, rhip, neck, lshoul,
						rshoul, lelb, relb, lknee, rknee, lhand, rhand, lfingertip, rfingertip, lankle, rankle, lfoot, rfoot, head;
					GetSkeletonJointPosition2(XN_SKEL_SPINEBASE, SpineBase);
					GetSkeletonJointPosition2(XN_SKEL_TORSO, torso);
					GetSkeletonJointPosition2(XN_SKEL_LEFT_HIP, lhip);
					GetSkeletonJointPosition2(XN_SKEL_RIGHT_HIP, rhip);
					GetSkeletonJointPosition2(XN_SKEL_NECK, neck);
					GetSkeletonJointPosition2(XN_SKEL_HEAD, head);
					GetSkeletonJointPosition2(XN_SKEL_LEFT_SHOULDER, lshoul);
					GetSkeletonJointPosition2(XN_SKEL_RIGHT_SHOULDER, rshoul);
					GetSkeletonJointPosition2(XN_SKEL_LEFT_ELBOW, lelb);
					GetSkeletonJointPosition2(XN_SKEL_RIGHT_ELBOW, relb);
					GetSkeletonJointPosition2(XN_SKEL_LEFT_HAND, lhand);
					GetSkeletonJointPosition2(XN_SKEL_RIGHT_HAND, rhand);
					GetSkeletonJointPosition2(XN_SKEL_LEFT_KNEE, lknee);
					GetSkeletonJointPosition2(XN_SKEL_RIGHT_KNEE, rknee);
					GetSkeletonJointPosition2(XN_SKEL_LEFT_FOOT, lfoot);
					GetSkeletonJointPosition2(XN_SKEL_RIGHT_FOOT, rfoot);

					GetSkeletonJointPosition2(XN_SKEL_LEFT_FINGERTIP, lfingertip);
					GetSkeletonJointPosition2(XN_SKEL_RIGHT_FINGERTIP, rfingertip);

					GetSkeletonJointPosition2(XN_SKEL_LEFT_KNEE, lknee);
					GetSkeletonJointPosition2(XN_SKEL_RIGHT_KNEE, rknee);
					GetSkeletonJointPosition2(XN_SKEL_LEFT_ANKLE, lankle);
					GetSkeletonJointPosition2(XN_SKEL_RIGHT_ANKLE, rankle);
					GetSkeletonJointPosition2(XN_SKEL_LEFT_FOOT, lfoot);
					GetSkeletonJointPosition2(XN_SKEL_RIGHT_FOOT, rfoot);


#ifdef _REVERSE_HAND
					XnSkeletonJointPosition tshoul, telb, thand;

					copy_joint_position(lshoul, tshoul);
					copy_joint_position(lelb, telb);
					copy_joint_position(lhand, thand);

					copy_joint_position(rshoul, lshoul);
					copy_joint_position(relb, lelb);
					copy_joint_position(rhand, lhand);

					copy_joint_position(tshoul, rshoul);
					copy_joint_position(telb, relb);
					copy_joint_position(thand, rhand);

					XnSkeletonJointPosition tfoot, tknee;

					copy_joint_position(lfoot, tfoot);
					copy_joint_position(lknee, tknee);

					copy_joint_position(rfoot, lfoot);
					copy_joint_position(rknee, lknee);

					copy_joint_position(tfoot, rfoot);
					copy_joint_position(tknee, rknee);

#endif
					if (start == false) {
						startpos.X = torso.position.X;
						startpos.Y = torso.position.Y;
						startpos.Z = torso.position.Z;
						start = true;
					}

					XnPoint3D pos;
					pos.X = -(torso.position.X - startpos.X);
					pos.Y = torso.position.Y - startpos.Y;
					pos.Z = -(torso.position.Z - startpos.Z);

					QMap mq;

					SIGKINECT_Linkage *linkage = (SIGKINECT_Linkage*)srv;

					if (linkage->grasp() == true && linkage->first() == true) {
						linkage->next();

						copy_joint_position(lshoul, ssld);
						copy_joint_position(lelb, selb);
						copy_joint_position(lhand, shand);

						copy_joint_position(lfoot, sfoot);
						copy_joint_position(lknee, sknee);


					}
					else if (linkage->grasp() == true) {
						copy_joint_position(ssld, lshoul);

						lelb.fConfidence = selb.fConfidence;
						lelb.position.X = selb.position.X;
						lelb.position.Z = selb.position.Z;

						// Žè‚Ìî•ñ‚ð•ÛŽ‚µ‚Ü‚·
						lhand.fConfidence = shand.fConfidence;
						lhand.position.X = shand.position.X;
						lhand.position.Z = shand.position.Z;

					}

					XnPoint3D khip_vec;
					if (DiffVec(khip_vec, rhip, lhip)) {
#ifdef _REAL_VERSION
						mq.insert(QMap::value_type("WAIST", CalcQuaternion(GetSigVec(HIP), khip_vec)));
#endif
						Quaternion rrootq = CalcQuaternion(khip_vec, GetSigVec(HIP));
						Quaternion rrootn = CalcQuaternion(khip_vec, GetSigVec(HIP));
#ifndef REAL_VERSION
						rrootq.qw = 1.0;
						rrootq.qx = 0.0;
						rrootq.qy = 0.0;
						rrootq.qz = 0.0;
#endif
#ifdef _VERBOSE
						printf("[%s][%d]WAIST(%f,%f,%f,%f)\n", __FUNCTION__, __LINE__, rrootq.qw, rrootq.qx, rrootq.qy, rrootq.qz);
#endif
						// ˜
						XnPoint3D kneck_vec;
						if (DiffVec(kneck_vec, torso, neck)) {
							RotVec(kneck_vec, rrootq, linkage->grasp());
							XnPoint3D sneck_vec = GetSigVec(WAIST);
#ifdef _REAL_VERSION
							mq.insert(QMap::value_type("WAIST_JOINT1", CalcQuaternion(sneck_vec, kneck_vec)));
#endif
							Quaternion rwaist = CalcQuaternion(kneck_vec, sneck_vec);
							Quaternion nwaist = CalcQuaternion(kneck_vec, sneck_vec);
#ifndef _REAL_VERSION
							rwaist.qw = 1.0;
							rwaist.qx = 0.0;
							rwaist.qy = 0.0;
							rwaist.qz = 0.0;
#endif

#ifdef _VERBOSE
							printf("[%s][%d]WAIST_JOINT1(%f,%f,%f,%f)\n", __FUNCTION__, __LINE__, rwaist.qw, rwaist.qx, rwaist.qy, rwaist.qz);
#endif

#ifdef _NOLINKAGE
							XnPoint3D khead_vec;
							if (DiffVec(khead_vec, neck, head)) {
								RotVec(khead_vec, rrootqn);
								RotVec(khead_vec, nwaist);

								Quaternion qhead = CalcQuaternion(sneck_vec, khead_vec);
#ifdef _VERBOSE
								printf("[%s][%d]HEAD_JOINT1(%f,%f,%f,%f)\n", __FUNCTION__, __LINE__, qhead.qw, qhead.qx, qhead.qy, qhead.qz);
#endif
								mq.insert(QMap::value_type("HEAD_JOINT1", qhead));
							}
#endif

#ifdef _NOLINKAGE
							XnPoint3D krsh_vec;
							if (DiffVec(krsh_vec, rshoul, relb)) {
								XnPoint3D srsh_vec = GetSigVec(RSHOULDER);

								RotVec(krsh_vec, rrootq);
								RotVec(krsh_vec, rwaist);

								mq.insert(QMap::value_type("RARM_JOINT2", CalcQuaternion(srsh_vec, krsh_vec)));
								Quaternion rrsh = CalcQuaternion(krsh_vec, srsh_vec); //‹t‰ñ“]

								XnPoint3D krel_vec;
								if (DiffVec(krel_vec, relb, rhand)) {
									RotVec(krel_vec, rrootq);
									RotVec(krel_vec, rwaist);
									RotVec(krel_vec, rrsh);

									mq.insert(QMap::value_type("RARM_JOINT3", CalcQuaternion(srsh_vec, krel_vec)));
								}
							}

							XnPoint3D klsh_vec;
							if (DiffVec(klsh_vec, lshoul, lelb)) {
								XnPoint3D slsh_vec = GetSigVec(LSHOULDER);

								RotVec(klsh_vec, rrootq);
								RotVec(klsh_vec, rwaist);

								mq.insert(QMap::value_type("LARM_JOINT2", CalcQuaternion(slsh_vec, klsh_vec)));
								Quaternion rlsh = CalcQuaternion(klsh_vec, slsh_vec); 
								//////////////////////////////////////////////


								XnPoint3D klel_vec;
								if (DiffVec(klel_vec, lelb, lhand)) {

									RotVec(klel_vec, rrootq);
									RotVec(klel_vec, rwaist);
									RotVec(klel_vec, rlsh);

									mq.insert(QMap::value_type("LARM_JOINT3", CalcQuaternion(slsh_vec, klel_vec)));
								}
							}
						}
#endif

#ifdef _LINKAGE

						XnPoint3D klsh_vec;   
						XnPoint3D krsh_vec_1;
						XnPoint3D krsh_vec_2;

						if (DiffVec(klsh_vec, lshoul, lelb) &&
							DiffVec(krsh_vec_1, lshoul, lelb) && 
							DiffVec(krsh_vec_2, lshoul, lelb))
						{
#ifndef _REVERSE_HAND
							XnPoint3D slsh_vec = GetSigVec(LSHOULDER); 
							XnPoint3D srsh_vec_1 = GetSigVec(LSHOULDER); 
							XnPoint3D srsh_vec_2 = GetSigVec(LSHOULDER);
#else
							XnPoint3D slsh_vec = GetSigVec(RSHOULDER); 
							XnPoint3D srsh_vec_1 = GetSigVec(RSHOULDER); 
							XnPoint3D srsh_vec_2 = GetSigVec(RSHOULDER); 
#endif
							RotVec(klsh_vec, rrootq, linkage->grasp());
							RotVec(klsh_vec, rwaist, linkage->grasp()); 
							RotVec(krsh_vec_1, rrootq, linkage->grasp());
							RotVec(krsh_vec_1, rwaist, linkage->grasp()); 
							RotVec(krsh_vec_2, rrootq, linkage->grasp());
							RotVec(krsh_vec_2, rwaist, linkage->grasp()); 
#ifdef _VERBOSE
							printf("krsh_vec_2(x:%f,y:%f,z:%f)\n", krsh_vec_2.X, krsh_vec_2.Y, krsh_vec_2.Z);
#endif
							Quaternion rlsht = CalcQuaternion(slsh_vec, klsh_vec);
							mq.insert(QMap::value_type((std::string(GRASP_ARM[LEFT]) + std::string("_JOINT2")).c_str(), rlsht));
							Quaternion rlsh = CalcQuaternion(klsh_vec, slsh_vec);

							Quaternion rrsht_1 = CalcQuaternion(krsh_vec_1, srsh_vec_1); 
							mq.insert(QMap::value_type((std::string(GRASP_ARM[RIGHT]) + std::string("_JOINT2-1")).c_str(), rrsht_1));
							Quaternion rrsh_1 = CalcQuaternion(srsh_vec_1, krsh_vec_1); 

#ifdef _OLD_VERSION
							Quaternion rrsht_2 = CalcQuaternion(srsh_vec_2, krsh_vec_2);
							mq.insert(QMap::value_type("RARM_JOINT2-2", rrsht_2));
							Quaternion rrsh_2 = CalcQuaternion(krsh_vec_2, srsh_vec_2);
#else
							Quaternion rrsht_2 = CalcQuaternion(krsh_vec_2, srsh_vec_2);
							mq.insert(QMap::value_type((std::string(GRASP_ARM[RIGHT]) + std::string("_JOINT2-2")).c_str(), rrsht_2));
							Quaternion rrsh_2 = CalcQuaternion(srsh_vec_2, krsh_vec_2);
#endif

#ifdef _VERBOSE
							for (int i = 0; i < 70; i++) printf("-%s", (i == 69 ? "\n" : ""));
							printf("[%s][%d]LARM_JOINT2  (%f,%f,%f,%f)\n", __FUNCTION__, __LINE__, rlsh.qw, rlsh.qx, rlsh.qy, rlsh.qz);
							printf("[%s][%d]RARM_JOINT2-1(%f,%f,%f,%f)\n", __FUNCTION__, __LINE__, rrsh_1.qw, rrsh_1.qx, rrsh_1.qy, rrsh_1.qz);
							printf("[%s][%d]RARM_JOINT2-2(%f,%f,%f,%f)\n", __FUNCTION__, __LINE__, rrsh_2.qw, rrsh_2.qx, rrsh_2.qy, rrsh_2.qz);
#endif
							XnPoint3D klel_vec; 
							XnPoint3D krel_vec_1;
							XnPoint3D krel_vec_2;

							if (DiffVec(klel_vec, lelb, lhand) &&
								DiffVec(krel_vec_1, lelb, lhand) && 
								DiffVec(krel_vec_2, lelb, lhand)) 
							{
								RotVec(klel_vec, rrootq, linkage->grasp()); 
								RotVec(klel_vec, rwaist, linkage->grasp());
								RotVec(klel_vec, rlsh, linkage->grasp());

								RotVec(krel_vec_1, rrootq); 
								RotVec(krel_vec_1, rwaist);
								RotVec_Xaxis_Reverse(krel_vec_1, rrsh_1, linkage->grasp()); 
#ifdef _OLD_VERSION
								RotVec(krel_vec_2, rrootq, linkage->grasp());
								RotVec(krel_vec_2, rwaist, linkage->grasp()); 
								RotVec(krel_vec_2, rrsh_2, linkage->grasp()); 
#else
								RotVec(krel_vec_2, rrootq, linkage->grasp()); 
								RotVec(krel_vec_2, rwaist, linkage->grasp()); 
								RotVec_Xaxis_Reverse(krel_vec_2, rrsh_2, linkage->grasp()); 
#endif
#ifdef _VERBOSE
								printf("krel_vec_2(x:%f,y:%f,z:%f)\n", krel_vec_2.X, krel_vec_2.Y, krel_vec_2.Z);
#endif
								Quaternion qlelb = CalcQuaternion(slsh_vec, klel_vec);
								mq.insert(QMap::value_type((std::string(GRASP_ARM[LEFT]) + std::string("_JOINT3")).c_str(), qlelb));

								Quaternion qrelb_1 = CalcQuaternion(srsh_vec_1, krel_vec_1);
								mq.insert(QMap::value_type((std::string(GRASP_ARM[RIGHT]) + std::string("_JOINT3-1")).c_str(), qrelb_1));

								Quaternion qrelb_2 = CalcQuaternion(srsh_vec_2, krel_vec_2);
								mq.insert(QMap::value_type((std::string(GRASP_ARM[RIGHT]) + std::string("_JOINT3-2")).c_str(), qrelb_2));

#ifdef _VERBOSE
								printf("[%s][%d]LARM_JOINT3  (%f,%f,%f,%f)\n", __FUNCTION__, __LINE__, qlelb.qw, qlelb.qx, qlelb.qy, qlelb.qz);
								printf("[%s][%d]RARM_JOINT3-1(%f,%f,%f,%f)\n", __FUNCTION__, __LINE__, qrelb_1.qw, qrelb_1.qx, qrelb_1.qy, qrelb_1.qz);
								printf("[%s][%d]RARM_JOINT3-2(%f,%f,%f,%f)\n", __FUNCTION__, __LINE__, qrelb_2.qw, qrelb_2.qx, qrelb_2.qy, qrelb_2.qz);
#endif
							}
						}
#endif

					} // end of waist

#ifdef _USE_FOOT
					XnPoint3D sleg_vec = GetSigVec(LEG);
#endif

#ifdef _WHOLE_BODY
					XnPoint3D krhip_vec;
					if (DiffVec(krhip_vec, rhip, rknee))
					{

						RotVec(krhip_vec, rrootq);

						mq.insert(QMap::value_type("RLEG_JOINT2", CalcQuaternion(sleg_vec, krhip_vec)));
						Quaternion rrhp = CalcQuaternion(krhip_vec, sleg_vec);
#ifdef _VERBOSE
						printf("[%s][%d]RLEG_JOINT2(%f,%f,%f,%f)\n", __FUNCTION__, __LINE__, rrhp.qw, rrhp.qx, rrhp.qy, rrhp.qz);
#endif

						XnPoint3D krknee_vec;
						if (DiffVec(krknee_vec, rknee, rfoot))
						{

							RotVec(krknee_vec, rrootq);
							RotVec(krknee_vec, rrhp);

							Quaternion qrleg = CalcQuaternion(sleg_vec, krknee_vec);
							mq.insert(QMap::value_type("RLEG_JOINT4", qrleg));
#ifdef _VERBOSE
							printf("[%s][%d]RLEG_JOINT4(%f,%f,%f,%f)\n", __FUNCTION__, __LINE__, qrleg.qw, qrleg.qx, qrleg.qy, qrleg.qz);
#endif

						}
					}
#endif

#ifdef _USE_FOOT
					XnPoint3D klhip_vec;  
					XnPoint3D krhip_vec_1; 
					XnPoint3D krhip_vec_2;

					if (DiffVec(klhip_vec, lhip, lknee) && 
						DiffVec(krhip_vec_1, lhip, lknee) && 
						DiffVec(krhip_vec_2, lhip, lknee))   
					{
						RotVec(klhip_vec, rrootq/*, linkage->grasp()*/); 
						RotVec_Zaxis(krhip_vec_1, rrootq, linkage->grasp()); 
						RotVec_Zaxis(krhip_vec_2, rrootq, linkage->grasp()); 

						Quaternion rlleg = CalcQuaternion(sleg_vec, klhip_vec);
						mq.insert(QMap::value_type((std::string(GRASP_LEG[LEFT]) + std::string("_JOINT2")).c_str(), rlleg));
						Quaternion rlhp = CalcQuaternion(klhip_vec, sleg_vec);

						Quaternion rrleg_1 = CalcQuaternion(krhip_vec_1, sleg_vec);
						mq.insert(QMap::value_type((std::string(GRASP_LEG[RIGHT]) + std::string("_JOINT2-1")).c_str(), rrleg_1));
						Quaternion rrhp_1 = CalcQuaternion(sleg_vec, krhip_vec_1); 

						Quaternion rrleg_2 = CalcQuaternion(krhip_vec_2, sleg_vec);
						mq.insert(QMap::value_type((std::string(GRASP_LEG[RIGHT]) + std::string("_JOINT2-2")).c_str(), rrleg_2));
						Quaternion rrhp_2 = CalcQuaternion(sleg_vec, krhip_vec_2); 

#ifdef _VERBOSE
						printf("[%s][%d]%s(%f,%f,%f,%f)\n", __FUNCTION__, __LINE__, (std::string(GRASP_LEG[LEFT]) + std::string("_JOINT2")).c_str(), rlhp.qw, rlhp.qx, rlhp.qy, rlhp.qz);
#endif

						XnPoint3D klknee_vec;
						XnPoint3D krknee_vec_1;
						XnPoint3D krknee_vec_2;

						if (DiffVec(klknee_vec, lknee, lfoot) &&
							DiffVec_Xaxis(krknee_vec_1, lknee, lfoot) &&
							DiffVec_Xaxis(krknee_vec_2, lknee, lfoot)) {

							RotVec(klknee_vec, rrootq/*, linkage->grasp()*/);
							RotVec(klknee_vec, rlhp/*,   linkage->grasp()*/);
							RotVec(krknee_vec_1, rrootq);
							RotVec(krknee_vec_1, rrhp_1);
							RotVec(krknee_vec_2, rrootq/*, linkage->grasp()*/);
							RotVec(krknee_vec_2, rrhp_2/*, linkage->grasp()*/);

							Quaternion qlleg = CalcQuaternion(sleg_vec, klknee_vec);
							mq.insert(QMap::value_type((std::string(GRASP_LEG[LEFT]) + std::string("_JOINT4")).c_str(), qlleg));
							Quaternion qrleg_1 = CalcQuaternion(sleg_vec, krknee_vec_1);
							mq.insert(QMap::value_type((std::string(GRASP_LEG[RIGHT]) + std::string("_JOINT4-1")).c_str(), qrleg_1));
							Quaternion qrleg_2 = CalcQuaternion(sleg_vec, krknee_vec_2);
							mq.insert(QMap::value_type((std::string(GRASP_LEG[RIGHT]) + std::string("_JOINT4-2")).c_str(), qrleg_2));

#ifdef _VERBOSE
							printf("[%s][%d]%s(%f,%f,%f,%f)\n", __FUNCTION__, __LINE__, (std::string(GRASP_LEG[LEFT]) + std::string("_JOINT4")).c_str(), qlleg.qw, qlleg.qx, qlleg.qy, qlleg.qz);
#endif

						}
					}
#endif


#ifdef _OLD_VERSION
					int connectedNum = srv->getConnectedControllerNum();
					printf("[%s][%d]connected controllers number:%d\n", __FUNCTION__, __LINE__, connectedNum);

					if (connectedNum > 0){
#endif

						float tx = (float)(pos.X * move_speed);
						float ty = (float)(pos.Y * move_speed);
						float tz = (float)(pos.Z * move_speed);

						tz = 0;

						std::string posx = FloatToString(tx);
						std::string posy = FloatToString(ty);
						std::string posz = FloatToString(tz);
						std::string pos_ = "POSITION";
						pos_ += " " + posx + "," + posy + "," + posz;

						std::string st = "KINECT ";
#ifdef _REAL_VERSION
						st += pos_;
#endif
						QMap::iterator it = mq.begin();

						while (it != mq.end())
						{
							st += " ";
							st += (GetStringFromQuaternion((*it).first.c_str(), (*it).second));
							it++;
						}
						st += " END";
						printf("st=\n%s\n",st.c_str());
#ifdef _VERBOSE
						for (int i = 0; i < 70; i++) printf("-%s", (i == 69 ? "\n" : ""));
						printf("[%s][%d]message to %s:%s\n", __FUNCTION__, __LINE__, agent_name.c_str(), st.c_str());
#endif
						srv->sendMsg(agent_name, st);
#ifdef _OLD_VERSION
						std::vector<std::string> names = srv->getAllConnectedEntitiesName();
						for (int i = 0; i < connectedNum; i++){
							srv->sendMsgToCtr(names[i].c_str(), st);
							printf("[%s][%d]entity:%s,info:%s\n", __FUNCTION__, __LINE__, names[i].c_str(), st.c_str());
						}
					}
#endif

				}
				cnt = speed;
			}
			cnt--;
		}
//#ifndef USE_GLES
//		glEnd();
//#endif
#ifndef _KINECT_PLAY
	}
#endif
}
}



void KinectApp::showVector4(std::string str, Vector4 vector4)
{
	printf("%s:\t", str.c_str());
	printf("w=%f\t", vector4.w);
	printf("x=%f\t", vector4.x);
	printf("y=%f\t", vector4.y);
	printf("z=%f\n", vector4.z);
}

void KinectApp::showXnSkeletonJointPosition(std::string str, XnSkeletonJointPosition point)
{
	printf("%s:", str.c_str());
	//printf("fconfidence=%f\t", point.fConfidence);
	printf("x=%f\t", point.position.X);//right for man:+ left    for man:-  
	printf("y=%f\t", point.position.Y);//down  for man:+ up      for man:-  
	printf("z=%f\t\n", point.position.Z);//back  for man:+ forward for man:-   
}



void KinectApp::showQuaternion(std::string str, Quaternion quaternion){
	double angle = acos(quaternion.qw) * 2 * 57.29578;
	char buf[256];
	sprintf_s(buf, "%s:\tangle=%f\nw=%f\tx=%f\ty=%f\tz=%f\n", str.c_str(), angle, quaternion.qw, quaternion.qx, quaternion.qy, quaternion.qz);
	//sprintf_s(buf, "%s:%f,%f,%f,%f\n", str.c_str(),quaternion.qw, quaternion.qx, quaternion.qy, quaternion.qz);

	//printf("degree=%f\n",angle);
	//printf("w=%f\t", quaternion.qw);
	//printf("x=%f\t", quaternion.qx);
	//printf("y=%f\t", quaternion.qy);
	//printf("z=%f\n", quaternion.qz);
	//LOG(buf);
	printf("%s", buf);

}

std::string KinectApp::strQuaternion(std::string str, Quaternion quaternion){
	double angle = acos(quaternion.qw) * 2 * 57.29578;
	char buf[256];
	//sprintf_s(buf, "%s:degree=%f\nw=%f\tx=%f\ty=%f\tz=%f\n", str.c_str(), angle, quaternion.qw, quaternion.qx, quaternion.qy, quaternion.qz);
	sprintf_s(buf, "%s:%f,%f,%f,%f ", str.c_str(), quaternion.qw, quaternion.qx, quaternion.qy, quaternion.qz);

	//printf("degree=%f\n",angle);
	//printf("w=%f\t", quaternion.qw);
	//printf("x=%f\t", quaternion.qx);
	//printf("y=%f\t", quaternion.qy);
	//printf("z=%f\n", quaternion.qz);
	//LOG(buf);
	//log->Write(buf);
	std::string strMsg = buf;
	return strMsg;
}

std::string KinectApp::FloatToString(float x)
{
	char tmp[32];
	sprintf_s(tmp, "%f", x);
	std::string str;
	str = std::string(tmp);
	return str;
}

std::string KinectApp::GetStringFromQuaternion(std::string jname, Quaternion q)
{
	std::string qw = FloatToString(q.qw);
	std::string qx = FloatToString(q.qx);
	std::string qy = FloatToString(q.qy);
	std::string qz = FloatToString(q.qz);
	std::string s = jname + " " + qw + "," + qx + "," + qy + "," + qz;

	return s;
}

std::string KinectApp::GetStringFromQuaternion_Vectror4(std::string jname, Vector4 q)
{
	std::string qw = FloatToString(q.w);
	std::string qx = FloatToString(q.x);
	std::string qy = FloatToString(q.y);
	std::string qz = FloatToString(q.z);
	std::string s = jname + ":" + qw + "," + qx + "," + qy + "," + qz;

	return s;
}

void KinectApp::drawLine(cv::Mat bodyImage, Joint joint1, Joint joint2, cv::Scalar color)
{
	//tlanslate camera coordinate system into Depth coordinate system
	//ComPtr<ICoordinateMapper> mapper;
	//ERROR_CHECK(kinect->get_CoordinateMapper(&mapper));
	cv::Point point1;
	BodyToScreen(joint1.Position, point1, width, height);
	//mapper->MapCameraPointToDepthSpace(joint1.Position, &point1);
	cv::Point point2;
	BodyToScreen(joint2.Position, point2, width, height);
	//mapper->MapCameraPointToDepthSpace(joint2.Position, &point2);
	cv::line(bodyImage, point1, point2, cv::Scalar(0, 0, 200), 20, 8, 0);
}


//http://wagashi1349.hatenablog.com/entry/2014/08/14/003618
void KinectApp::BodyToScreen(const CameraSpacePoint& bodyPoint, cv::Point& point, int  width, int height)
{
	//printf("start BodyToScreen\n");
	ComPtr<ICoordinateMapper> mapper;
	ERROR_CHECK(kinect->get_CoordinateMapper(&mapper));
	DepthSpacePoint depthPoint = { 0 };
	mapper->MapCameraPointToDepthSpace(bodyPoint, &depthPoint);
	//printf("get coordinate\n");
	//int screenPointX = (int)depthPoint.X;// static_cast<float>(depthPoint.X * width) / 512;
	//int screenPointY = (int)depthPoint.Y;// static_cast<float>(depthPoint.Y * height) / 424;
	//int screenPointX = (int)((depthPoint.X * width) / 512);
	//int screenPointY = (int)((depthPoint.Y * height) / 424);
	point.x = (int)((depthPoint.X * width) / 512);
	point.y = (int)((depthPoint.Y * height) / 424);
	//printf("end BodyToScreen\n");
	//return cv::Point(screenPointX, screenPointY);
}

void KinectApp::drawEllipse(cv::Mat& bodyImage, const Joint& joint, int r, const cv::Scalar& color)
{
	//tlanslate camera coordinate system into Depth coordinate system
	//ComPtr<ICoordinateMapper> mapper;
	//ERROR_CHECK(kinect->get_CoordinateMapper(&mapper));

	//DepthSpacePoint point = {0};
	//mapper->MapCameraPointToDepthSpace(joint.Position, &point);
	//DepthSpacePoint depthPoint = { 0 };
	//coordinateMapper->MapCameraPointToDepthSpace(bodyPoint, &depthPoint);
	cv::Point point;
	BodyToScreen(joint.Position, point, width, height);
	cv::circle(bodyImage, point, r, color, -1);
	char buf[128];
	sprintf_s(buf, "%d", joint.JointType);
	//cv::putText(bodyImage, buf, cv::Point((int)point.X, (int)point.Y), CV_FONT_HERSHEY_SIMPLEX, 1.2, cv::Scalar(0, 0, 200), 2, CV_AA);

}

void KinectApp::drawHandState(cv::Mat& bodyImage, Joint joint, TrackingConfidence handConfidence, HandState handState)
{
	const int R = 40;

	if (handConfidence != TrackingConfidence::TrackingConfidence_High){
		return;
	}

	//tlanslate camera coordinate system into Depth coordinate system
	//ComPtr<ICoordinateMapper> mapper;
	//ERROR_CHECK(kinect->get_CoordinateMapper(&mapper));

	//DepthSpacePoint point;
	//mapper->MapCameraPointToDepthSpace(joint.Position, &point);
	cv::Point point;
	BodyToScreen(joint.Position, point, width, height);

	//open
	if (handState == HandState::HandState_Open){
		cv::circle(bodyImage, point, R, cv::Scalar(0, 255, 255), R / 4);
	}
	//scissors
	else if (handState == HandState::HandState_Lasso){
		cv::circle(bodyImage, point, R, cv::Scalar(255, 0, 255), R / 4);
	}
	//close
	else if (handState == HandState::HandState_Closed){
		cv::circle(bodyImage, point, R, cv::Scalar(255, 255, 0), R / 4);
	}
}

/**
Quaternion KinectApp::CalcQuaternion(XnPoint3D kvec, XnPoint3D svec)
{
	Quaternion q;
	if (kvec.X == svec.X && kvec.Y == svec.Y && kvec.Z == svec.Z)
	{
		q.qw = 1; q.qx = 0; q.qy = 0; q.qz = 0;
		return q;
	}


	double x = kvec.Y*svec.Z - kvec.Z*svec.Y;
	double y = kvec.Z*svec.X - kvec.X*svec.Z;
	double z = kvec.X*svec.Y - kvec.Y*svec.X;

	double sum = sqrt(x*x + y*y + z*z);
	x = x / sum;
	y = y / sum;
	z = z / sum;

	double angle = acos(kvec.X*svec.X + kvec.Y*svec.Y + kvec.Z*svec.Z);
	q.qw = (float)(cos(angle / 2));
	q.qx = (float)(x*sin(angle / 2));
	q.qy = (float)(y*sin(angle / 2));
	q.qz = (float)(z*sin(angle / 2));

	return q;
}
*/


Vector4 KinectApp::convertQuaternion2Vector4(Quaternion quaternion)
{
	Vector4 vector4;
	vector4.w = quaternion.qw;
	vector4.x = quaternion.qx;
	vector4.y = quaternion.qy;
	vector4.z = quaternion.qz;
	return vector4;
}

/**
*[out] XnPoint3D &rvec: relative position info with direction
*[in] XnSkeletonJointPosition jvec: parent joint position info with confidence
*[in] XnSkeletonJointPosition kvec: child joint  position info with confidence
*/
bool KinectApp::DiffVec(XnPoint3D &rvec, XnSkeletonJointPosition jvec, XnSkeletonJointPosition kvec)
{
	if (jvec.fConfidence < 0.5 || kvec.fConfidence < 0.5)
	{
		return false;
	}

	//in SIGVerse:
	//left for man:+
	//up for man:+
	//forward for man:+
	rvec.X = -(kvec.position.X - jvec.position.X);
	rvec.Y = kvec.position.Y - jvec.position.Y;
	rvec.Z = -(kvec.position.Z - jvec.position.Z);

	double length = sqrt(rvec.X*rvec.X + rvec.Y*rvec.Y + rvec.Z*rvec.Z);
	rvec.X = (XnFloat)(rvec.X / length);
	rvec.Y = (XnFloat)(rvec.Y / length);
	rvec.Z = (XnFloat)(rvec.Z / length);
	return true;
}

//// Make Rotational quaternion      
Quaternion KinectApp::MakeRotationalQuaternion(double   radian, double AxisX, double AxisY, double AxisZ)
{
	Quaternion   ans;
	double   norm;
	double   ccc, sss;

	ans.qw = ans.qx = ans.qy = ans.qz = 0.0;

	norm = AxisX *  AxisX + AxisY *  AxisY + AxisZ *  AxisZ;
	if (norm <= 0.0) return ans;

	norm = 1.0 / sqrt(norm);
	AxisX *= norm;
	AxisY *= norm;
	AxisZ *= norm;

	ccc = cos(0.5 * radian);
	sss = sin(0.5 * radian);

	ans.qw = (float)(ccc);
	ans.qx = (float)(sss * AxisX);
	ans.qy = (float)(sss * AxisY);
	ans.qz = (float)(sss * AxisZ);

	return   ans;
}

void KinectApp::showXnPoint3D(std::string name, XnPoint3D vector){
	printf("%s: x=%f, y=%f, z=%f\n", name.c_str(), vector.X, vector.Y, vector.Z);
}

void KinectApp::GetSkeletonJointPosition2(XnSkeletonJoint eJoint, XnSkeletonJointPosition& Joint){
	switch (eJoint)
	{
	case XN_SKEL_HEAD:
		//1
		if (joints[3].TrackingState == 0){ Joint.fConfidence = 0; }
		else{ Joint.fConfidence = 1; }
		Joint.position.X = joints[3].Position.X;
		Joint.position.Y = joints[3].Position.Y;
		Joint.position.Z = joints[3].Position.Z;
		break;
	case XN_SKEL_NECK:
		//2
		if (joints[2].TrackingState == 0){ Joint.fConfidence = 0; }
		else{ Joint.fConfidence = 1; }
		Joint.position.X = joints[2].Position.X;
		Joint.position.Y = joints[2].Position.Y;
		Joint.position.Z = joints[2].Position.Z;
		break;
	case XN_SKEL_SPINEBASE:
		//2
		if (joints[0].TrackingState == 0){ Joint.fConfidence = 0; }
		else{ Joint.fConfidence = 1; }
		Joint.position.X = joints[0].Position.X;
		Joint.position.Y = joints[0].Position.Y;
		Joint.position.Z = joints[0].Position.Z;
		break;
	case XN_SKEL_TORSO:
		if (joints[1].TrackingState == 0){ Joint.fConfidence = 0; }
		else{ Joint.fConfidence = 1; }
		Joint.position.X = joints[1].Position.X;
		Joint.position.Y = joints[1].Position.Y;
		Joint.position.Z = joints[1].Position.Z;
		break;
	case XN_SKEL_WAIST:
		if (joints[0].TrackingState == 0){ Joint.fConfidence = 0; }
		else{ Joint.fConfidence = 1; }
		Joint.position.X = joints[0].Position.X;
		Joint.position.Y = joints[0].Position.Y;
		Joint.position.Z = joints[0].Position.Z;
		break;
	case XN_SKEL_LEFT_COLLAR:
		if (joints[20].TrackingState == 0){ Joint.fConfidence = 0; }
		else{ Joint.fConfidence = 1; }
		Joint.position.X = joints[20].Position.X;
		Joint.position.Y = joints[20].Position.Y;
		Joint.position.Z = joints[20].Position.Z;
		break;
	case XN_SKEL_LEFT_SHOULDER:
		//6
		if (joints[4].TrackingState == 0){ Joint.fConfidence = 0; }
		else{ Joint.fConfidence = 1; }
		Joint.position.X = joints[4].Position.X;
		Joint.position.Y = joints[4].Position.Y;
		Joint.position.Z = joints[4].Position.Z;
		break;

	case XN_SKEL_LEFT_ELBOW:
		//7
		if (joints[5].TrackingState == 0){ Joint.fConfidence = 0; }
		else{ Joint.fConfidence = 1; }
		Joint.position.X = joints[5].Position.X;
		Joint.position.Y = joints[5].Position.Y;
		Joint.position.Z = joints[5].Position.Z;
		break;
	case XN_SKEL_LEFT_WRIST:
		if (joints[6].TrackingState == 0){ Joint.fConfidence = 0; }
		else{ Joint.fConfidence = 1; }
		Joint.position.X = joints[6].Position.X;
		Joint.position.Y = joints[6].Position.Y;
		Joint.position.Z = joints[6].Position.Z;
		break;
	case XN_SKEL_LEFT_HAND:
		if (joints[7].TrackingState == 0){ Joint.fConfidence = 0; }
		else{ Joint.fConfidence = 1; }
		Joint.position.X = joints[7].Position.X;
		Joint.position.Y = joints[7].Position.Y;
		Joint.position.Z = joints[7].Position.Z;
		break;
	case XN_SKEL_LEFT_FINGERTIP:
		if (joints[21].TrackingState == 0){ Joint.fConfidence = 0; }
		else{ Joint.fConfidence = 1; }
		Joint.position.X = joints[21].Position.X;
		Joint.position.Y = joints[21].Position.Y;
		Joint.position.Z = joints[21].Position.Z;
		break;
	case XN_SKEL_RIGHT_COLLAR:
		if (joints[20].TrackingState == 0){ Joint.fConfidence = 0; }
		else{ Joint.fConfidence = 1; }
		Joint.position.X = joints[20].Position.X;
		Joint.position.Y = joints[20].Position.Y;
		Joint.position.Z = joints[20].Position.Z;
		break;
	case XN_SKEL_RIGHT_SHOULDER:
		if (joints[8].TrackingState == 0){ Joint.fConfidence = 0; }
		else{ Joint.fConfidence = 1; }
		Joint.position.X = joints[8].Position.X;
		Joint.position.Y = joints[8].Position.Y;
		Joint.position.Z = joints[8].Position.Z;
		break;
	case XN_SKEL_RIGHT_ELBOW:
		if (joints[9].TrackingState == 0){ Joint.fConfidence = 0; }
		else{ Joint.fConfidence = 1; }
		Joint.position.X = joints[9].Position.X;
		Joint.position.Y = joints[9].Position.Y;
		Joint.position.Z = joints[9].Position.Z;
		break;
	case XN_SKEL_RIGHT_WRIST:
		if (joints[10].TrackingState == 0){ Joint.fConfidence = 0; }
		else{ Joint.fConfidence = 1; }
		Joint.position.X = joints[10].Position.X;
		Joint.position.Y = joints[10].Position.Y;
		Joint.position.Z = joints[10].Position.Z;
		break;
	case XN_SKEL_RIGHT_HAND:
		if (joints[11].TrackingState == 0){ Joint.fConfidence = 0; }
		else{ Joint.fConfidence = 1; }
		Joint.position.X = joints[11].Position.X;
		Joint.position.Y = joints[11].Position.Y;
		Joint.position.Z = joints[11].Position.Z;
		break;
	case XN_SKEL_RIGHT_FINGERTIP:
		if (joints[23].TrackingState == 0){ Joint.fConfidence = 0; }
		else{ Joint.fConfidence = 1; }
		Joint.position.X = joints[23].Position.X;
		Joint.position.Y = joints[23].Position.Y;
		Joint.position.Z = joints[23].Position.Z;
		break;

	case XN_SKEL_LEFT_HIP:
		//17
		if (joints[12].TrackingState == 0){ Joint.fConfidence = 0; }
		else{ Joint.fConfidence = 1; }
		Joint.position.X = joints[12].Position.X;
		Joint.position.Y = joints[12].Position.Y;
		Joint.position.Z = joints[12].Position.Z;
		//printf("Moving left hip X  : %f ---- Y : %f ----- Z : %f ----- end  \n", Joint.position.X, Joint.position.Y, Joint.position.Z);
		break;
	case XN_SKEL_LEFT_KNEE:
		if (joints[13].TrackingState == 0){ Joint.fConfidence = 0; }
		else{ Joint.fConfidence = 1; }
		Joint.position.X = joints[13].Position.X;
		Joint.position.Y = joints[13].Position.Y;
		Joint.position.Z = joints[13].Position.Z;
		break;
	case XN_SKEL_LEFT_ANKLE:
		if (joints[14].TrackingState == 0){ Joint.fConfidence = 0; }
		else{ Joint.fConfidence = 1; }
		Joint.position.X = joints[14].Position.X;
		Joint.position.Y = joints[14].Position.Y;
		Joint.position.Z = joints[14].Position.Z;
		break;
	case XN_SKEL_LEFT_FOOT:
		if (joints[15].TrackingState == 0){ Joint.fConfidence = 0; }
		else{ Joint.fConfidence = 1; }
		Joint.position.X = joints[15].Position.X;
		Joint.position.Y = joints[15].Position.Y;
		Joint.position.Z = joints[15].Position.Z;
		break;
	case XN_SKEL_RIGHT_HIP:
		//21
		if (joints[16].TrackingState == 0){ Joint.fConfidence = 0; }
		else{ Joint.fConfidence = 1; }
		Joint.position.X = joints[16].Position.X;
		Joint.position.Y = joints[16].Position.Y;
		Joint.position.Z = joints[16].Position.Z;
		//printf("Moving right hip X  : %f ---- Y : %f ----- Z : %f ----- end  \n", Joint.position.X, Joint.position.Y, Joint.position.Z);
		break;
	case XN_SKEL_RIGHT_KNEE:
		if (joints[17].TrackingState == 0){ Joint.fConfidence = 0; }
		else{ Joint.fConfidence = 1; }
		Joint.position.X = joints[17].Position.X;
		Joint.position.Y = joints[17].Position.Y;
		Joint.position.Z = joints[17].Position.Z;
		break;
	case XN_SKEL_RIGHT_ANKLE:
		if (joints[18].TrackingState == 0){ Joint.fConfidence = 0; }
		else{ Joint.fConfidence = 1; }
		Joint.position.X = joints[18].Position.X;
		Joint.position.Y = joints[18].Position.Y;
		Joint.position.Z = joints[18].Position.Z;
		break;
	case XN_SKEL_RIGHT_FOOT:
		if (joints[19].TrackingState == 0){ Joint.fConfidence = 0; }
		else{ Joint.fConfidence = 1; }
		Joint.position.X = joints[19].Position.X;
		Joint.position.Y = joints[19].Position.Y;
		Joint.position.Z = joints[19].Position.Z;
		break;
	default:
		break;
	}
}

void KinectApp::showPosition(std::string strMsg, CameraSpacePoint& point)
{
	printf("%s:\t%f\t%f\t%f\n", strMsg, point.X, point.Y, point.Z);
}


void KinectApp::transQuaternion(Quaternion q, int i, int j, int k)
{
	//do not input i, j, k anything except for 1 or -1
	if ((i == 1 || i == -1) && (j == 1 || j == -1) && (k == 1 || k == -1))
	{
		q.qx = q.qx * i;
		q.qy = q.qy * j;
		q.qz = q.qz * k;
	}
	else
	{
		//printf("i, j, k  must be 1 or -1");
		exit(1);
	}
}
void  KinectApp::showJointOrientations(std::string str, JointOrientation jointOrientation)
{
	double angle = acos(jointOrientation.Orientation.w) * 2 * 57.29578; \
		printf("%s:\tangle=%f\nw=%f\tx=%f\ty=%f\tz=%f\n",
		str.c_str(),
		angle,
		jointOrientation.Orientation.w,
		jointOrientation.Orientation.x,
		jointOrientation.Orientation.y,
		jointOrientation.Orientation.z
		);
}
Quaternion KinectApp::transQuaternion2(Quaternion q, int i, int j, int k)
{
	Quaternion q2;
	q2.qw = 0.0f;
	q2.qx = 0.0f;
	q2.qy = 0.0f;
	q2.qz = 0.0f;
	//do not input i, j, k anything except for 1 or -1
	if ((i == 1 || i == -1) && (j == 1 || j == -1) && (k == 1 || k == -1))
	{
		q2.qw = q.qw;
		q2.qx = q.qx * i;
		q2.qy = q.qy * j;
		q2.qz = q.qz * k;
	}
	else{
		printf("i, j, k  must be 1 or -1");
		//exit(1);
	}
	return q2;
}

//copy from SceneDrawer.cpp
Quaternion KinectApp::MultiQuaternion(Quaternion p, Quaternion q)
{
	Quaternion r;
	r.qw = p.qw * q.qw - p.qx * q.qx - p.qy * q.qy - p.qz * q.qz;
	r.qx = p.qw * q.qx + p.qx * q.qw + p.qy * q.qz - p.qz * q.qy;
	r.qy = p.qw * q.qy - p.qx * q.qz + p.qy * q.qw + p.qz * q.qx;
	r.qz = p.qw * q.qz + p.qx * q.qy - p.qy * q.qx + p.qz * q.qw;

	return r;
}

XnPoint3D KinectApp::convertCameraSpacePoint2Point2XnPoint3D(CameraSpacePoint csp)
{
	XnPoint3D xp3;
	xp3.X = csp.X;
	xp3.Y = csp.Y;
	xp3.Z = csp.Z;
	//printf("x=%f, y=%f, z=%f\n",xp3.X,xp3.Y,xp3.Z);
	return xp3;
}

Quaternion KinectApp::convertVector42Quaternion(Vector4 vector4)
{
	Quaternion quaternion;
	quaternion.qw = vector4.w;
	quaternion.qx = vector4.x;
	quaternion.qy = vector4.y;
	quaternion.qz = vector4.z;
	return quaternion;
}

void KinectApp::drawString(cv::Mat& bodyImage, const Joint& joint, int r, const cv::Scalar& color)
{
	if (joint.TrackingState == TrackingState::TrackingState_Tracked)
	{
		//tlanslate camera coordinate system into Depth coordinate system
		ComPtr<ICoordinateMapper> mapper;
		ERROR_CHECK(kinect->get_CoordinateMapper(&mapper));

		DepthSpacePoint point;
		mapper->MapCameraPointToDepthSpace(joint.Position, &point);

		//cv::circle(bodyImage, cv::Point((int)point.X, (int)point.Y), r, color, -1);
		char buf[128];
		sprintf_s(buf, "%d", joint.JointType);
		cv::putText(bodyImage, buf, cv::Point(width - (int)point.X, (int)point.Y), CV_FONT_HERSHEY_SIMPLEX, 1.2, cv::Scalar(0, 0, 200), 2, CV_AA);
	}
}

/*!
* @brief copy JointPosition
*/
void KinectApp::copy_joint_position(XnSkeletonJointPosition& s, XnSkeletonJointPosition& t)
{
	t.fConfidence = s.fConfidence;
	t.position.X = s.position.X;
	t.position.Y = s.position.Y;
	t.position.Z = s.position.Z;
}


/*!
* ------------------------------------------------------------------------------------------
* @brief 
* @param[in/out] XnPoint3D&
* @param[in]     Quaterion&
* @param[in]     bool       
* ------------------------------------------------------------------------------------------
*/
void KinectApp::RotVec_Zaxis(XnPoint3D &v, Quaternion q, bool axis1) {

	if (axis1 == true) {
		q.qy = 0.0;
	}

	float rw = v.X *  q.qx + v.Y *  q.qy + v.Z * -q.qz;
	float rx = v.X *  q.qw + v.Y *  q.qz + v.Z *  q.qy;
	float ry = v.X * -q.qz + v.Y *  q.qw + v.Z * -q.qx;
	float rz = v.X * -q.qy + v.Y *  q.qx + v.Z *  q.qw;

	v.X = q.qx *  rw + q.qy * -rz + q.qz * -ry + q.qw *  rx;
	v.Y = q.qx *  rz + q.qy *  rw + q.qz *  rx + q.qw *  ry;
	v.Z = q.qx *  ry + q.qy * -rx + q.qz *  rw + q.qw * -rz;

}

/*!
* ------------------------------------------------------------------------------------------
* @brief
* @param[in/out] XnPoint3D& 
* @param[in]     Quaterion& 
* @param[in]     bool      
* ------------------------------------------------------------------------------------------
*/
void KinectApp::RotVec_Xaxis(XnPoint3D &v, Quaternion q, bool axis1) {

	if (axis1 == true) {
		q.qy = 0.0;
	}

	float rw = v.X * -q.qx + v.Y *  q.qy + v.Z *  q.qz;
	float rx = v.X *  q.qw + v.Y * -q.qz + v.Z *  q.qy;
	float ry = v.X *  q.qz + v.Y *  q.qw + v.Z *  q.qx;
	float rz = v.X * -q.qy + v.Y * -q.qx + v.Z *  q.qw;

	v.X = q.qx *  rw + q.qy *  rz + q.qz * -ry + q.qw * -rx;
	v.Y = q.qx * -rz + q.qy *  rw + q.qz * -rx + q.qw *  ry;
	v.Z = q.qx *  ry + q.qy *  rx + q.qz *  rw + q.qw *  rz;

}

/*!
* ------------------------------------------------------------------------------------------
* @brief 
* @param[in/out] XnPoint3D& 
* @param[in]     Quaterion&
* @param[in]     bool     
* ------------------------------------------------------------------------------------------
*/
void KinectApp::RotVec_Yaxis(XnPoint3D &v, Quaternion q, bool axis1) {

	if (axis1 == true) {
		q.qy = 0.0;
	}

	float rw = v.X *  q.qx + v.Y * -q.qy + v.Z *  q.qz;
	float rx = v.X *  q.qw + v.Y * -q.qz + v.Z * -q.qy;
	float ry = v.X *  q.qz + v.Y *  q.qw + v.Z * -q.qx;
	float rz = v.X *  q.qy + v.Y *  q.qx + v.Z *  q.qw;

	v.X = q.qx *  rw + q.qy *  rz + q.qz *  ry + q.qw * rx;
	v.Y = q.qx * -rz + q.qy *  rw + q.qz *  rx + -q.qw * ry;
	v.Z = q.qx * -ry + q.qy * -rx + q.qz *  rw + q.qw * rz;

}

/*!
* ------------------------------------------------------------------------------------------
* @deprecated
* @brief 
* @param[in/out] XnPoint3D&
* @param[in]     Quaterion& 
* @param[in]     bool  
* ------------------------------------------------------------------------------------------
*/
void KinectApp::RotVec_Reverse(XnPoint3D &v, Quaternion q, bool axis1) {

	float rw = v.X * -q.qx + v.Y * -q.qy + v.Z * -q.qz;
	float rx = v.X *  q.qw + v.Y *  q.qz + v.Z * -q.qy;
	float ry = v.X * -q.qz + v.Y *  q.qw + v.Z *  q.qx;
	float rz = v.X *  q.qy + v.Y * -q.qx + v.Z *  q.qw;

	v.X = q.qx *  rw + q.qy * -rz + q.qz *  ry + q.qw * -rx;
	v.Y = q.qx *  rz + q.qy *  rw + q.qz * -rx + q.qw * -ry;
	v.Z = q.qx * -ry + q.qy *  rx + q.qz *  rw + q.qw * -rz;

}

/*!
* ------------------------------------------------------------------------------------------
* @brief
* @param[in/out] XnPoint3D&
* @param[in]     Quaterion& 
* @param[in]     bool      
* ------------------------------------------------------------------------------------------
*/
void KinectApp::RotVec_Xaxis_Reverse(XnPoint3D &v, Quaternion q, bool axis1) {

	if (axis1 == true) {
		q.qy = 0.0;
	}

	// ------------------------------------------------------------
	float rw = v.X *  q.qx + v.Y * -q.qy + v.Z * -q.qz;
	float rx = v.X *  q.qw + v.Y *  q.qz + v.Z * -q.qy;
	float ry = v.X * -q.qz + v.Y *  q.qw + v.Z * -q.qx;
	float rz = v.X *  q.qy + v.Y *  q.qx + v.Z *  q.qw;

	// ------------------------------------------------------------
	v.X = q.qx *  rw + q.qy * -rz + q.qz *  ry + q.qw *  rx;
	v.Y = q.qx *  rz + q.qy *  rw + q.qz *  rx + q.qw * -ry;
	v.Z = q.qx * -ry + q.qy * -rx + q.qz *  rw + q.qw * -rz;

}

/*!
* ------------------------------------------------------------------------------------------
* @deprecated
* @brief
* @param[in/out] XnPoint3D&
* @param[in]     Quaterion& 
* @param[in]     bool       
* ------------------------------------------------------------------------------------------
*/
void KinectApp::RotVec_Zaxis_Reverse(XnPoint3D &v, Quaternion q, bool axis1) 
{
	float rw = v.X * -q.qx + v.Y * -q.qy + v.Z *  q.qz;
	float rx = v.X *  q.qw + v.Y * -q.qz + v.Z * -q.qy;
	float ry = v.X *  q.qz + v.Y *  q.qw + v.Z *  q.qx;
	float rz = v.X *  q.qy + v.Y * -q.qx + v.Z *  q.qw;
	v.X = q.qx *  rw + q.qy *  rz + q.qz *  ry + q.qw * -rx;
	v.Y = q.qx * -rz + q.qy *  rw + q.qz * -rx + q.qw * -ry;
	v.Z = q.qx * -ry + q.qy *  rx + q.qz *  rw + q.qw *  rz;

}

/*!
* ------------------------------------------------------------------------------------------
* @deprecated
* @brief
* @param[in/out] XnPoint3D& 
* @param[in]     Quaterion&
* @param[in]     bool       
* ------------------------------------------------------------------------------------------
*/
void KinectApp::RotVec_Yaxis_Reverse(XnPoint3D &v, Quaternion q, bool axis1) {


	float rw = v.X * -q.qx + v.Y *  q.qy + v.Z * -q.qz;
	float rx = v.X *  q.qw + v.Y *  q.qz + v.Z *  q.qy;
	float ry = v.X * -q.qz + v.Y *  q.qw + v.Z *  q.qx;
	float rz = v.X * -q.qy + v.Y * -q.qx + v.Z *  q.qw;

	v.X = q.qx *  rw + q.qy * -rz + q.qz * -ry + q.qw * -rx;
	v.Y = q.qx *  rz + q.qy *  rw + q.qz * -rx + q.qw *  ry;
	v.Z = q.qx *  ry + q.qy *  rx + q.qz *  rw + q.qw * -rz;

}

/*!
* ------------------------------------------------------------------------------------------
* @deprecated
* @brief
* @param[in/out] XnPoint3D& 
* @param[in]     Quaterion& 
* @param[in]     bool      
* ------------------------------------------------------------------------------------------
*/
void KinectApp::RotVec_YZaxis_Reverse(XnPoint3D &v, Quaternion q, bool axis1) 
{

	float rw = v.X * -q.qx + v.Y *  q.qy + v.Z *  q.qz;
	float rx = v.X *  q.qw + v.Y * -q.qz + v.Z *  q.qy;
	float ry = v.X *  q.qz + v.Y *  q.qw + v.Z *  q.qx;
	float rz = v.X * -q.qy + v.Y * -q.qx + v.Z *  q.qw;

	v.X = q.qx *  rw + q.qy *  rz + q.qz * -ry + q.qw * -rx;
	v.Y = q.qx * -rz + q.qy *  rw + q.qz * -rx + q.qw *  ry;
	v.Z = q.qx *  ry + q.qy *  rx + q.qz *  rw + q.qw *  rz;

}

/*!
* ------------------------------------------------------------------------------------------
* @brief 
* @param[out] XnPoint3D&             
* @param[in]  XnSkeltonJointPosition 
* @param[in]  XnSkeltonJointPosition 
* ------------------------------------------------------------------------------------------
*/
bool  KinectApp::DiffVec_Xaxis(XnPoint3D &rvec, XnSkeletonJointPosition jvec, XnSkeletonJointPosition kvec) {

	if (jvec.fConfidence < 0.5 || kvec.fConfidence < 0.5) {
		return false;
	}
	rvec.X = (kvec.position.X - jvec.position.X);
	rvec.Y = kvec.position.Y - jvec.position.Y;
	rvec.Z = -(kvec.position.Z - jvec.position.Z);

	float length = sqrt(rvec.X * rvec.X + rvec.Y * rvec.Y + rvec.Z * rvec.Z);
	rvec.X = rvec.X / length;
	rvec.Y = rvec.Y / length;
	rvec.Z = rvec.Z / length;

	return true;
}

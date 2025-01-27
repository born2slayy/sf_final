#include <ros/ros.h>
#include <sensor_msgs/Image.h>
#include <sensor_msgs/PointCloud2.h>
#include <sensor_msgs/CameraInfo.h>
#include <tf/transform_listener.h>
#include <image_transport/image_transport.h>
#include <opencv2/highgui/highgui.hpp>
#include <cv_bridge/cv_bridge.h>
#include <Eigen/Geometry>

#include <message_filters/subscriber.h>
#include <message_filters/synchronizer.h>
#include <message_filters/sync_policies/approximate_time.h>


#include <pcl/common/common.h>
#include <pcl/common/pca.h>
#include <pcl/filters/extract_indices.h>
#include <pcl/filters/statistical_outlier_removal.h>
#include <pcl/filters/voxel_grid.h>
#include <pcl/io/pcd_io.h>
#include <pcl/kdtree/kdtree.h>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl/segmentation/extract_clusters.h>
#include <pcl_conversions/pcl_conversions.h>

#include <pcl/filters/passthrough.h>

#include <pcl/sample_consensus/ransac.h>
#include <pcl/sample_consensus/sac_model.h>
#include <pcl/sample_consensus/model_types.h>
#include <pcl/sample_consensus/method_types.h>
#include <pcl/sample_consensus/sac_model_line.h>
#include <pcl/sample_consensus/sac_model_plane.h>
#include <pcl/sample_consensus/sac_model_sphere.h>

#include <pcl/filters/statistical_outlier_removal.h>

#include <opencv2/calib3d.hpp>
#include <opencv2/core/eigen.hpp>

#include <tf/transform_broadcaster.h>
#include <tf_conversions/tf_eigen.h>

#include <iostream>
#include <fstream>

#include <std_msgs/Float64MultiArray.h>
#include <std_msgs/Int32.h>
#include <std_msgs/Float64.h>

#include <yolov7_ros/DetectionInfo.h> //bounding box coordinate, info msg
#include <vision_msgs/Detection2DArray.h>
#include <vision_msgs/BoundingBox2D.h>
#include <cam_lidar_calib/sf_Info_del.h>


//PointCloud2 및 이미지 메시지의 동기화
typedef message_filters::sync_policies::ApproximateTime<sensor_msgs::PointCloud2,
                                                        sensor_msgs::Image,yolov7_ros::DetectionInfo> SyncPolicy;

// 라이다와 카메라 데이터 융합 및 투사
class lidarImageProjection {
private:

    ros::NodeHandle nh;
    ros::Publisher pub;

    //chaewon
    ros::Publisher pubA;
    ros::Publisher pubB;


    //라이다 포인트 클라우드
    message_filters::Subscriber<sensor_msgs::PointCloud2> *cloud_sub;
    //이미지
    message_filters::Subscriber<sensor_msgs::Image> *image_sub;

    //chaewon
    //bounding box subscribe
    message_filters::Subscriber<vision_msgs::Detection2DArray> *detection_sub;

    //동기화
    message_filters::Synchronizer<SyncPolicy> *sync;

    //게시
    ros::Publisher cloud_pub;
    ros::Publisher image_pub;
    //센서퓨전 publish용
    ros::Publisher sf_info_pub;
    //chaewon
    ros::Publisher sf_info_pub_A;
    ros::Publisher sf_info_pub_B;
    



    //벡터 로드
    cv::Mat c_R_l, tvec;
    cv::Mat rvec;
    std::string result_str;
    
    //카메라와 라이더 좌표 프레임 간의 변환
    Eigen::Matrix4d C_T_L, L_T_C;
    Eigen::Matrix3d C_R_L, L_R_C;
    Eigen::Quaterniond C_R_L_quatn, L_R_C_quatn;
    Eigen::Vector3d C_t_L, L_t_C;

    //평면 투영 여부
    bool project_only_plane;

    //카메라 왜곡변수 등
    cv::Mat projection_matrix;
    cv::Mat distCoeff;

    //라이다 및 카메라 좌표 프레임과 2D 이미지 포인트에 3D 포인트를 저장
    std::vector<cv::Point3d> objectPoints_L, objectPoints_C;
    std::vector<cv::Point2d> imagePoints;

    //투영 후 출력 포인트 클라우드를 저장
    sensor_msgs::PointCloud2 out_cloud_ros;

    //라이다의 프레임 ID
    std::string lidar_frameId;
  
    //cam frame ID
    std::string cam_frameId;

    //카메라 및 라이다 데이터 게시
    std::string camera_in_topic;
    std::string lidar_in_topic;

    //출력 포인트 클라우드를 PCL 형식으로 저장
    pcl::PointCloud<pcl::PointXYZRGB> out_cloud_pcl;

    //입력 이미지
    cv::Mat image_in;
    cv::Mat bounding_img;

    //투영할 포인트의 거리 차단
    //라이더에서 카메라로 투영을 수행할 때 센서에서 너무 멀거나 가까운 포인트를 필터링
    int dist_cut_off;

    //카메라 구성 파일의 경로와 이미지의 너비 및 높이
    std::string cam_config_file_path;
    int image_width, image_height;

    //카메라 이름
    std::string camera_name;

    
    

public:
    //멤버변수 초기화 및 ros연결
    lidarImageProjection() {
        camera_in_topic = readParam<std::string>(nh, "camera_in_topic");
        lidar_in_topic = readParam<std::string>(nh, "lidar_in_topic");
        dist_cut_off = readParam<int>(nh, "dist_cut_off");
        camera_name = readParam<std::string>(nh, "camera_name");

        cloud_sub =  new message_filters::Subscriber<sensor_msgs::PointCloud2>(nh, lidar_in_topic, 1);
        // image_sub = new message_filters::Subscriber<sensor_msgs::Image>(nh, camera_in_topic, 1);
        image_sub = new message_filters::Subscriber<sensor_msgs::Image>(nh, camera_in_topic, 1);
        detection_sub = new message_filters::Subscriber<vision_msgs::Detection2DArray>(nh, "/yolov7/delivery_multi", 1);
        ros::Duration delay = ros::Time::now() - msg.header.stamp;
        ROS_INFO("욜로->센퓨 전송 Delay: %f seconds", delay.toSec());
        
        //camera_in_topic 이었던 부분
        std::string lidarOutTopic = camera_in_topic + "/velodyne_out_cloud";
        cloud_pub = nh.advertise<sensor_msgs::PointCloud2>(lidarOutTopic, 1);
        std::string imageOutTopic = camera_in_topic + "/projected_image";
        image_pub = nh.advertise<sensor_msgs::Image>(imageOutTopic, 1);
        sensor_msgs::PointCloud2ConstPtr cloud_msg;

        // 라이다, 카메라, detection_info 토픽의 메시지를 동기화
        //typedef message_filters::sync_policies::ApproximateTime<sensor_msgs::PointCloud2, sensor_msgs::Image, yolov7_ros::DetectionInfo> SyncPolicy;
        //message_filters::Synchronizer<SyncPolicy> sync(SyncPolicy(10), *cloud_sub, *image_sub, *detection_sub);
        //sync.registerCallback(boost::bind(&lidarImageProjection::callback, _1, _2, _3));

        //제어한테 class id, 거리 distance, x, y ,z 를 publish 
        sf_info_pub = nh.advertise<cam_lidar_calib::sf_Info_del>("sf_info_del", 1000);

        //chaewon
        // sf_info_pub_A = nh.advertise<cam_lidar_calib::sf_Info_del>("sf_info_del_A", 1000);
        // sf_info_pub_B = nh.advertise<cam_lidar_calib::sf_Info_del>("sf_info_del_B", 1000);
       
    



        message_filters::Synchronizer<SyncPolicy>* sync = new message_filters::Synchronizer<SyncPolicy>(SyncPolicy(10), *cloud_sub, *image_sub, *detection_sub);
        sync->registerCallback(boost::bind(&lidarImageProjection::callback, this, _1, _2, _3));

        C_T_L = Eigen::Matrix4d::Identity();
        c_R_l = cv::Mat::zeros(3, 3, CV_64F);
        tvec = cv::Mat::zeros(3, 1, CV_64F);

        result_str = readParam<std::string>(nh, "result_file");
        project_only_plane = readParam<bool>(nh, "project_only_plane");

        projection_matrix = cv::Mat::zeros(3, 3, CV_64F);
        distCoeff = cv::Mat::zeros(5, 1, CV_64F);

        std::ifstream myReadFile(result_str.c_str());
        std::string word;
        int i = 0;
        int j = 0;
        while (myReadFile >> word){
            C_T_L(i, j) = atof(word.c_str());
            j++;
            if(j>3) {
                j = 0;
                i++;
            }
        }
        L_T_C = C_T_L.inverse();

        C_R_L = C_T_L.block(0, 0, 3, 3);
        C_t_L = C_T_L.block(0, 3, 3, 1);

        L_R_C = L_T_C.block(0, 0, 3, 3);
        L_t_C = L_T_C.block(0, 3, 3, 1);

        cv::eigen2cv(C_R_L, c_R_l);
        C_R_L_quatn = Eigen::Quaterniond(C_R_L);
        L_R_C_quatn = Eigen::Quaterniond(L_R_C);
        cv::Rodrigues(c_R_l, rvec);
        cv::eigen2cv(C_t_L, tvec);

        cam_config_file_path = readParam<std::string>(nh, "cam_config_file_path");
        readCameraParams(cam_config_file_path,
                         image_height,
                         image_width,
                         distCoeff,
                         projection_matrix);
    }

    void readCameraParams(std::string cam_config_file_path,
                          int &image_height,
                          int &image_width,
                          cv::Mat &D,
                          cv::Mat &K) {
        cv::FileStorage fs_cam_config(cam_config_file_path, cv::FileStorage::READ);
        if(!fs_cam_config.isOpened())
            std::cerr << "Error: Wrong path: " << cam_config_file_path << std::endl;
        fs_cam_config["image_height"] >> image_height;
        fs_cam_config["image_width"] >> image_width;
        fs_cam_config["k1"] >> D.at<double>(0);
        fs_cam_config["k2"] >> D.at<double>(1);
        fs_cam_config["p1"] >> D.at<double>(2);
        fs_cam_config["p2"] >> D.at<double>(3);
        fs_cam_config["k3"] >> D.at<double>(4);
        fs_cam_config["fx"] >> K.at<double>(0, 0);
        fs_cam_config["fy"] >> K.at<double>(1, 1);
        fs_cam_config["cx"] >> K.at<double>(0, 2);
        fs_cam_config["cy"] >> K.at<double>(1, 2);
    }

    template <typename T>
    T readParam(ros::NodeHandle &n, std::string name){
        T ans;
        if (n.getParam(name, ans)){
            ROS_INFO_STREAM("Loaded " << name << ": " << ans);
        } else {
            ROS_ERROR_STREAM("Failed to load " << name);
            n.shutdown();
        }
        return ans;
    }

    
    pcl::PointCloud<pcl::PointXYZ >::Ptr planeFilter(float x_min,float x_max,float y_min,float y_max,const sensor_msgs::PointCloud2ConstPtr &cloud_msg) {

        // sensor_msgs::PointCloud2 메시지 포인터(cloud_msg)를 입력으로 받아 in_cloud로 변환
        // PCL 라이브러리를 사용하여 포인트 클라우드를 추가로 처리가능
        //pcl::PointCloud<pcl::PointXYZ>::Ptr in_cloud(new pcl::PointCloud<pcl::PointXYZ>);
        pcl::PointCloud<pcl::PointXYZ>::Ptr in_cloud(new pcl::PointCloud<pcl::PointXYZ>);
        pcl::fromROSMsg(*cloud_msg, *in_cloud);

        //포인트 클라우드 결과를 저장하는 데 사용되는 포인터들
        pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_filtered_x(new pcl::PointCloud<pcl::PointXYZ>);
        pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_filtered_y(new pcl::PointCloud<pcl::PointXYZ>);
        pcl::PointCloud<pcl::PointXYZ >::Ptr plane(new pcl::PointCloud<pcl::PointXYZ>);
        pcl::PointCloud<pcl::PointXYZ >::Ptr plane_filtered(new pcl::PointCloud<pcl::PointXYZ>);

        /// Pass through filters
        //x 및 y 좌표를 기준으로 점을 필터링
        //0718
        /**/
        pcl::PassThrough<pcl::PointXYZ> pass_x;
        pass_x.setInputCloud(in_cloud);
        //pass_x 필터는 x 값을 [0.0, 5.0] 범위로 제한
        pass_x.setFilterFieldName("x");
        pass_x.setFilterLimits(0.0, 6.0);
        pass_x.filter(*cloud_filtered_x);
        pcl::PassThrough<pcl::PointXYZ> pass_y;
        pass_y.setInputCloud(cloud_filtered_x);
        //pass_y 필터는 y 값을 [-1.25, 1.25] 범위로 제한
        pass_y.setFilterFieldName("y");
        pass_y.setFilterLimits(-1.25, 1.25);
        pass_y.filter(*cloud_filtered_y);

        /// Plane Segmentation
        //RANSAC 알고리즘을 사용하여 평면 분할을 수행
        pcl::SampleConsensusModelPlane<pcl::PointXYZ>::Ptr model_p(new pcl::SampleConsensusModelPlane<pcl::PointXYZ>(cloud_filtered_y));
        // RANSAC을 적용하여 평면 파라미터를 추정
        pcl::RandomSampleConsensus<pcl::PointXYZ> ransac(model_p);
        ransac.setDistanceThreshold(0.01);
        ransac.computeModel();
        std::vector<int> inliers_indicies;
        //결집점 인덱스
        ransac.getInliers(inliers_indicies);
        //평면 포인트 클라우드에 해당 점을 복사
        pcl::copyPointCloud<pcl::PointXYZ>(*cloud_filtered_y, inliers_indicies, *plane);

        /// Statistical Outlier Removal
        //평면 포인트 클라우드에서 통계적 이상값 제거
        pcl::StatisticalOutlierRemoval<pcl::PointXYZ> sor;
        sor.setInputCloud(plane);
        //로컬 포인트 밀도를 기반으로 이상값을 식별
        sor.setMeanK (50);
        sor.setStddevMulThresh (1);
        sor.filter (*plane_filtered);

        //minpa
        // Filter the points based on bounding boxes
        pcl::PointCloud<pcl::PointXYZ>::Ptr filtered_cloud(new pcl::PointCloud<pcl::PointXYZ>);
        
        for (const pcl::PointXYZ& point : plane_filtered->points) {
            if (point.x >= x_min && point.x <= x_max && point.y >= y_min && point.y <= y_max) {
                //ROS_INFO("여기Lidar : x = %lf , y = %lf", point.x,point.y );
                //ROS_INFO("Box: x_min=%f,y_min=%f,x_max=%f,y_max=%f",x_min,y_min,x_max,y_max);

                filtered_cloud->points.push_back(point);
            }
        }
        filtered_cloud->width = filtered_cloud->points.size();
        filtered_cloud->height = 1;
        filtered_cloud->is_dense = true;

        //
        return plane_filtered;

    }
    //입력 RGB 이미지(cv::Mat rgb)와 2D 포인트(cv::Point2d xy_f)를 파라미터로 받아 cv::Vec3b 색상 값을 반환
    cv::Vec3b atf(cv::Mat rgb, cv::Point2d xy_f){
        //각 채널(R, G, B)의 색상 값 합계를 저장
        cv::Vec3i color_i;
        // 채널 0으로 초기화
        color_i.val[0] = color_i.val[1] = color_i.val[2] = 0;

        //xy_f에서 x 및 y 값을 추출
        int x = xy_f.x;
        int y = xy_f.y;

        // 2x2  중첩
        //현재 (x+col, y+row) 좌표가 이미지 경계 내에 있는지 확인
        for (int row = 0; row <= 1; row++){
            for (int col = 0; col <= 1; col++){
                if((x+col)< rgb.cols && (y+row) < rgb.rows) {
                    //좌표가 유효하면 함수는 입력 이미지에서 현재 위치의 픽셀 색상(cv::Vec3b c)을 검색
                    cv::Vec3b c = rgb.at<cv::Vec3b>(cv::Point(x + col, y + row));
                    for (int i = 0; i < 3; i++){
                        //각 채널의 색상 값(c.val[i])을 color_i.val[i]에 누적
                        color_i.val[i] += c.val[i];
                    }
                }
            }
        }

        //평균 색상 값을 저장
        cv::Vec3b color;
        for (int i = 0; i < 3; i++){
            //채널 합계를 4(2x2)로 나누고 평균값을 해당 채널에 색으로 할당
            color.val[i] = color_i.val[i] / 4;
        }
        return color;
    }

    
    //라이다 프레임과 카메라 프레임 사이의 변환
    void publishTransforms() {
        static tf::TransformBroadcaster br;
        tf::Transform transform;
        tf::Quaternion q;
        //라이다-카메라 간 회전 쿼터니언 변환
        tf::quaternionEigenToTF(L_R_C_quatn, q);
        //라이다 변환 벡터L_T_C
        transform.setOrigin(tf::Vector3(L_t_C(0), L_t_C(1), L_t_C(2)));
        transform.setRotation(q);
        //회전과 이동
        br.sendTransform(tf::StampedTransform(transform, ros::Time::now(), lidar_frameId, camera_name));
        //br.sendTransform(tf::StampedTransform(transform, ros::Time::now(), lidar_frameId, cam_frameId));
    }

    //atf 함수에서 얻은 RGB 값을 기반으로 컬러 포인트로 out_cloud_pcl 포인트 클라우드 구성
    void colorPointCloud() {
        //out_cloud_pcl 포인트 클라우드의 기존 포인트를 지우고 objectPoints_L의 수에 맞게 크기를 조정
        out_cloud_pcl.points.clear();
        out_cloud_pcl.resize(objectPoints_L.size());
        
        // atf 함수에서 RGB 색상 값을 검색
        for(size_t i = 0; i < objectPoints_L.size(); i++) {
            cv::Vec3b rgb = atf(image_in, imagePoints[i]);
            //구한 RGB 값을 사용하여 새로운 pcl::PointXYZRGB 포인트(pt_rgb)를 생성
            //ROS_INFO("i = %ld", i);
            //ROS_INFO("ImagePoints : (%lf, %lf) ", imagePoints[i].x, imagePoints[i].y);
        
        
            pcl::PointXYZRGB pt_rgb(rgb.val[2], rgb.val[1], rgb.val[0]);
            //pt_rgb 포인트의 XYZ 좌표는 objectPoints_L의 값으로 설정
            pt_rgb.x = objectPoints_L[i].x;
            pt_rgb.y = objectPoints_L[i].y;
            pt_rgb.z = objectPoints_L[i].z;
            //pt_rgb 포인트가 out_cloud_pcl 포인트 클라우드에 추가
            out_cloud_pcl.push_back(pt_rgb);
        }
    }

    void colorLidarPointsOnImage(double min_range, double max_range) {
        //각 imagePoints(2D 이미지 포인트)를 반복하고 objectPoints_C에서 해당 XYZ 좌표를 검색
        for(size_t i = 0; i < imagePoints.size(); i++) {
            double X = objectPoints_C[i].x;
            double Y = objectPoints_C[i].y;
            double Z = objectPoints_C[i].z;
            double range = sqrt(X*X + Y*Y + Z*Z);
            //지정된 범위(최소 범위 및 최대 범위) 내에서 선형 보간을 사용하여 원의 빨간색 및 녹색 색상 값을 계산
            //가까운 지점일수록 빨간색 커짐
            double red_field = 255*(range - min_range)/(max_range - min_range);
            //먼 지점일수록 초록색 값 커짐
            double green_field = 255*(max_range - range)/(max_range - min_range);
            //image_in 이미지에 반지름이 2인 원을 그림
            //가까우면 빨간색, 멀면 초록색
            cv::circle(image_in, imagePoints[i], 2,
                       CV_RGB(red_field, green_field, 0), -1, 1, 0);

        }
    }

    void callback(const sensor_msgs::PointCloud2ConstPtr &cloud_msg,
                  const sensor_msgs::ImageConstPtr &image_msg, const vision_msgs::Detection2DArray &detect_msg) {
        //라이더 메시지의 frame_id를 lidar_frameId 변수에 할당
        lidar_frameId = cloud_msg->header.frame_id;
        cam_frameId = image_msg->header.frame_id;

        //초기화
        objectPoints_L.clear();
        objectPoints_C.clear();
        imagePoints.clear();
        
        //변환
        publishTransforms();
        //수신된 이미지 메시지를 cv_bridge::toCvShare를 사용하여 cv::Mat 이미지로 변환
        image_in = cv_bridge::toCvShare(image_msg, "bgr8")->image;
        cv::Scalar color(255, 0, 255); // BGR color for Purple

        //카메라의 시야각(fov_x 및 fov_y)을 계산
        double fov_x, fov_y;
        fov_x = 2*atan2(image_width, 2*projection_matrix.at<double>(0, 0))*180/CV_PI;
        fov_y = 2*atan2(image_height, 2*projection_matrix.at<double>(1, 1))*180/CV_PI;

        //최대 범위는 음의 무한대, 최소 범위는 양의 무한대로 초기화
        //라이더 포인트의 최대 및 최소 범위 값을 추적
        double max_range, min_range;
        max_range = -INFINITY;
        min_range = INFINITY;

        //project_only_plane 플래그가 설정되어 있으면 이 함수는 planeFilter 함수를 호출하여 
        // 라이다 포인트를 필터링하고 지상면을 나타내는 포인트 클라우드를 얻음
        pcl::PointCloud<pcl::PointXYZ>::Ptr in_cloud(new pcl::PointCloud<pcl::PointXYZ>);
        // std::vector<geometry_msgs::Point> bounding_boxes = detect_msg->bounding_boxes;
        // int num_boxes = detect_msg->bounding_boxes.size();
        
        float x_min, x_max, y_min, y_max, box_center;

        std::vector<int> lidar_points_count(num_boxes/4, 0);  // 각 bounding box 내의 라이다 포인트 수를 저장할 배열 선언


        // 배열 선언(라바콘 바닥)
        std::vector<float> l_y_max(num_boxes/4, 0);
        // 배열 선언(거리)
        std::vector<double> distance(num_boxes/4, 0);
        // 배열 선언 가로 
        std::vector<double> sf_x(num_boxes/4, 0);  
        std::vector<double> sf_y(num_boxes/4, 0);  
        std::vector<double> sf_z(num_boxes/4, 0);  

        //double distance;
        //int pub_id;
        std::vector<int> pub_id(num_boxes/4, 0);

        // // A publish chaewon
        // // 배열 선언(거리)
        // std::vector<double> distance_A(num_boxes/4, 0);
        // // 배열 선언 가로 
        // std::vector<double> sf_x_A(num_boxes/4, 0);  
        // std::vector<double> sf_y_A(num_boxes/4, 0);  
        // std::vector<double> sf_z_A(num_boxes/4, 0);  

        // //double distance;
        // //int pub_id;
        // std::vector<int> pub_id_A(num_boxes/4, 0);

        // // B publish chaewon
        // // 배열 선언(거리)
        // std::vector<double> distance_B(num_boxes/4, 0);
        // // 배열 선언 가로 
        // std::vector<double> sf_x_B(num_boxes/4, 0);  
        // std::vector<double> sf_y_B(num_boxes/4, 0);  
        // std::vector<double> sf_z_B(num_boxes/4, 0);  

        // //double distance;
        // //int pub_id;
        // std::vector<int> pub_id_B(num_boxes/4, 0);
    

        //0718
        //in_cloud = planeFilter(x_min, x_max, y_min, y_max, cloud_msg);

        // 카메라 내부 파라미터 로드
        cv::Mat cameraMatrix = cv::Mat::eye(3, 3, CV_64F);
        cameraMatrix.at<double>(0, 0) = 658.9686053230556; // fx
        cameraMatrix.at<double>(1, 1) = 662.9395005428793; // fy
        cameraMatrix.at<double>(0, 2) = 305.82001455317686; // cx
        cameraMatrix.at<double>(1, 2) = 252.5114738799476; // cy

        cv::Point3d camera_frame_min;  // 모든 좌표를 0으로 초기화
        cv::Point3d camera_frame_max;  // 모든 좌표를 0으로 초기화

        camera_frame_min = cv::Point3d(0.0, 0.0, 0.0);
        camera_frame_max = cv::Point3d(0.0, 0.0, 0.0);

        
        //bool hasLidarPointsInsideBoundingBox = false;

        for (const auto& detection : detect_msg.detections) {
        // 바운딩 박스 정보 추출
            int k;

            
            const vision_msgs::BoundingBox2D& bbox = detection.bbox;                
            x_min = bbox.center.x - bbox.size_x / 2.0;
            x_max = bbox.center.x + bbox.size_x / 2.0;
            y_min = bbox.center.y - bbox.size_y / 2.0;
            y_max = bbox.center.y + bbox.size_y / 2.0;

            if(project_only_plane) {
                ROS_INFO("Project only palne");
                in_cloud = planeFilter(x_min,x_max,y_min,y_max,cloud_msg);
                for(size_t i = 0; i < in_cloud->points.size(); i++) {
                    //필터링된 평면의 3D 라이더 포인트로 업데이트
                    objectPoints_L.push_back(cv::Point3d(in_cloud->points[i].x, in_cloud->points[i].y, in_cloud->points[i].z));
                }
                //3D 라이더 포인트를 이미지 평면에 투영, 결과 2D 이미지 포인트가 imagePoints 벡터에 저장
                cv::projectPoints(objectPoints_L, rvec, tvec, projection_matrix, distCoeff, imagePoints, cv::noArray());
            } 
            else {
                pcl::PCLPointCloud2 *cloud_in = new pcl::PCLPointCloud2;
                pcl_conversions::toPCL(*cloud_msg, *cloud_in);
                pcl::fromPCLPointCloud2(*cloud_in, *in_cloud);
                ROS_INFO("---------------");
                //ROS_INFO("Box %d : x_min=%f,y_min=%f,x_max=%f,y_max=%f",(i/4)+1,x_min,y_min,x_max,y_max);
                //in_cloud 포인트 클라우드의 라이더 포인트를 반복
                for(size_t i = 0; i < in_cloud->points.size(); i++) {

                    // Reject points behind the LiDAR(and also beyond certain distance)
                    //라이더 뒤에 있거나 특정 거리를 벗어난 라이더 포인트는 건너뜀
                    //pointCloud_L[0] >= x_min && pointCloud_L[0] <= x_max && pointCloud_L[1]>= y_min && pointCloud_L[1] <= y_max
                    //if(in_cloud->points[i].x < 0 || in_cloud->points[i].x > dist_cut_off)
                    if(in_cloud->points[i].x > dist_cut_off)
                        continue;
                    //ROS_INFO("Lidar : x = %lf , y = %lf , z= %lf",in_cloud->points[i].x,in_cloud->points[i].y,in_cloud->points[i].z );
                    //각 라이더 포인트에 대해 변환 매트릭스 C_T_L을 사용하여 라이더 프레임에서 카메라 프레임으로 포인트를 변환
                    Eigen::Vector4d pointCloud_L;
                    pointCloud_L[0] = in_cloud->points[i].x;
                    pointCloud_L[1] = in_cloud->points[i].y;
                    pointCloud_L[2] = in_cloud->points[i].z;
                    pointCloud_L[3] = 1;

                    Eigen::Vector3d pointCloud_C;
                    pointCloud_C = C_T_L.block(0, 0, 3, 4)*pointCloud_L;

                    double X = pointCloud_C[0];
                    double Y = pointCloud_C[1];
                    double Z = pointCloud_C[2];




                    ////카메라 프레임에 대한 라이다 포인트의 각도(Xangle 및 Yangle)를 계산
                    double Xangle = atan2(X, Z)*180/CV_PI;
                    double Yangle = atan2(Y, Z)*180/CV_PI;


                    //카메라의 시야각(fov_x 및 fov_y) 내에 있는지 확인
                    if(Xangle < -fov_x/2 || Xangle > fov_x/2)

                    if(Yangle < -fov_y/2 || Yangle > fov_y/2)
                        continue;
                    

                    
                    //라이더 포인트의 범위는 XYZ 좌표를 사용하여 계산
                    double range = sqrt(X*X + Y*Y + Z*Z);

                    //ROS_INFO("range : range=%lf",range);

                    //min_range 및 max_range 변수 업데이트
                    if(range > max_range) {
                        max_range = range;
                        //ROS_INFO("in if : max_range=%lf",max_range);

                    }
                    if(range < min_range) {
                        min_range = range;
                        // double X = pointCloud_C[0];
                        // double Y = pointCloud_C[1];
                        // double Z = pointCloud_C[2];
                        //ROS_INFO("in else : min_range=%lf",min_range);
                        double Z = 0;

                        double X  =  cameraMatrix.at<double>(0, 0) * pointCloud_C[0] / pointCloud_C[2] + cameraMatrix.at<double>(0, 2);
                        double Y =  cameraMatrix.at<double>(1, 1)* pointCloud_C[1] / pointCloud_C[2]+ cameraMatrix.at<double>(1, 2);

                    }
                    
                    X  = cameraMatrix.at<double>(0, 0) * pointCloud_C[0] / pointCloud_C[2] + cameraMatrix.at<double>(0, 2);
                    Y =  cameraMatrix.at<double>(1, 1)* pointCloud_C[1] / pointCloud_C[2]+ cameraMatrix.at<double>(1, 2);
                    Z = 0;

                    
                    
                    //bounding box 좌표를 cam_calib 계수 넣어서 pointCloud_C랑 비교하는 코드
                    
                    if (X >= x_min && X <= x_max && Y>= y_min && Y<= y_max){
                        //ROS_INFO("1_Lidar_L : x = %lf , y = %lf , z= %lf",in_cloud->points[i].x,in_cloud->points[i].y,in_cloud->points[i].z );
                        //ROS_INFO("in bounding Box: x_min=%f,y_min=%f,x_max=%f,y_max=%f",xbool hasLidarPointsInsideBoundingBox = false;_min,y_min,x_max,y_max);
                        //&& X==box_center
                        //hasLidarPointsInsideBoundingBox = true;
                        // 왼쪽 모서리 끝, 즉 x 최소 y최대 
                        //우리가 지금 중간 최하단
                        if(X <= (x_min+50)){

                                l_y_max[k]=Y;
                                distance[k]=pointCloud_L[0];  //x값 거리

                                /*
                                sf_x[k] = pointCloud_L[1];  //라이다에서는 y(가로)
                                sf_y[k] = pointCloud_L[2];  //라이다에서는 z(세로)
                                sf_z[k] = pointCloud_L[0];  //라이다에서는 x(거리)
                                */

                                sf_x[k] = pointCloud_L[0];  
                                sf_y[k] = pointCloud_L[1]; 
                                sf_z[k] = pointCloud_L[2];  

                                pub_id[k] = detect_msg->class_id[k];

                           
                        }
                        

                        objectPoints_L.push_back(cv::Point3d(pointCloud_L[0], pointCloud_L[1], pointCloud_L[2]));
                        objectPoints_C.push_back(cv::Point3d(pointCloud_C[0], pointCloud_C[1], pointCloud_C[2]));  
                        //ROS_INFO("in boundingBox Lidar_C : X=%lf,Y=%lf,Z=%lf",pointCloud_C[0],pointCloud_C[1],pointCloud_C[2]);
                    }
                

                }
                 //라이더 포인트(objectPoints_L)를 이미지 평면에 투영
                // 결과 2D 이미지 포인트가 imagePoints 벡터에 저장

                // 바운딩 박스 내에 라이다 포인트가 있는지 확인
                if (objectPoints_L.size() > 0 && objectPoints_C.size() > 0) {
                    // Project lidar points onto image plane
                    cv::projectPoints(objectPoints_L, rvec, tvec, projection_matrix, distCoeff, imagePoints, cv::noArray());
                    // Draw bounding box on image
                    cv::rectangle(image_in, cv::Point(x_min, y_min), cv::Point(x_max, y_max), color, 3);
                }

                //cv::projectPoints(objectPoints_L, rvec, tvec, projection_matrix, distCoeff, imagePoints, cv::noArray());
                //cv::rectangle(image_in, cv::Point(x_min, y_min), cv::Point(x_max, y_max), color, 3);
            }
            //ROS_INFO("Box num : %d ,class num : %d, lidar y min : %lf, distance: %lf",k,pub_id,l_y_max[k], distance[k]);
            ROS_INFO("Box num : %d ,class num : %d, sf_x: %lf, sf_y:%lf, sf_z: %lf, lidar y min : %lf, distance: %lf",k,pub_id[k],sf_x[k],sf_y[k],sf_z[k], l_y_max[k], distance[k]);

            cv::circle(image_in, cv::Point(box_center, l_y_max[k]), 12, CV_RGB(0, 0, 255), -1, 1, 0);



        }
        /// Color the Point Cloud
        //colorPointCloud 함수를 호출하여 이미지에서 얻은 RGB 값을 기반으로 포인트 클라우드(out_cloud_pcl)에 색
        colorPointCloud();

        //색상이 지정된 포인트 클라우드(out_cloud_pcl)는 센서_msgs::PointCloud2 메시지(out_cloud_ros)로 변환
        pcl::toROSMsg(out_cloud_pcl, out_cloud_ros);
        //헤더 프레임 ID와 타임스탬프가 라이더 메시지와 일치하도록 설정
        out_cloud_ros.header.frame_id = cloud_msg->header.frame_id;
        out_cloud_ros.header.stamp = cloud_msg->header.stamp;

        //컬러 포인트 클라우드 메시지는 cloud_pub 게시자를 사용하여 게시
        cloud_pub.publish(out_cloud_ros);

        /// Color Lidar Points on the image a/c to distance
        // colorLidarPointsOnImage 함수가 호출되어 라이더 포인트의 범위(min_range 및 max_range)에 따라 
        // 이미지(image_in)에 컬러 점을 오버레이
        colorLidarPointsOnImage(min_range, max_range);
        //컬러 이미지(image_in)가 센서_msgs::이미지 메시지(msg)로 변환되고 image_pub 퍼블리셔를 사용하여 게시
        
        sensor_msgs::ImagePtr msg =
                cv_bridge::CvImage(std_msgs::Header(), "bgr8", image_in).toImageMsg();
        image_pub.publish(msg);


        //publish
        cam_lidar_calib::sf_Info_del sf_msg;
        // cam_lidar_calib::sf_Info_del sf_msg_A;
        // cam_lidar_calib::sf_Info_del sf_msg_B;
            //msg.header.stamp = ros::Time::now();
            //msg.header.frame_id = "lidar_frame";  // Replace with your frame id

        //num_boxes 추가
    
        sf_msg.num_boxes = num_boxes/4;
        sf_msg.pub_id = pub_id;
        sf_msg.sf_x = sf_x;
        sf_msg.sf_y = sf_y;
        sf_msg.sf_z = sf_z;
        sf_msg.distance = distance;
        sf_info_pub.publish(sf_msg);
        
        // sf_msg_A.num_boxes = num_boxes/4;
        // sf_msg_A.pub_id = pub_id_A;
        // sf_msg_A.sf_x = sf_x_A;
        // sf_msg_A.sf_y = sf_y_A;
        // sf_msg_A.sf_z = sf_z_A;
        // sf_msg_A.distance = distance_A;
        // sf_info_pub_A.publish(sf_msg_A);
    
        // sf_msg_B.num_boxes = num_boxes/4;
        // sf_msg_B.pub_id = pub_id_B;
        // sf_msg_B.sf_x = sf_x_B;
        // sf_msg_B.sf_y = sf_y_B;
        // sf_msg_B.sf_z = sf_z_B;
        // sf_msg_B.distance = distance_B;
        // sf_info_pub_B.publish(sf_msg_B);


    }
};

int main(int argc, char** argv) {
    ros::init(argc, argv, "cam_lidar_proj_del");
    lidarImageProjection lidar_image_projection;
    ros::spin();
    return 0;
}
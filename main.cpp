
#include <librealsense2/rs.h>
#include <librealsense2/rs.hpp>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <iostream>
#include <vector>
#include <zconf.h>
#include <Volume.hpp>
#include <Fusion.hpp>
#include <Raycast.hpp>
#include <Recorder.h>
#include <MeshWriter.h>
#include <KinectVirtualSensor.h>

#include "VirtualSensor.h"
#include "icp.h"
#include "Frame.h"

Fusion fusion;
Raycast raycast;
VirtualSensor sensor;
// KinectVirtualSensor sensor(PROJECT_DATA_DIR + std::string("/sample0"), 5 );
//Recorder rec;

//TODO this should be moved to one File containing all data_declarations class
struct Config{

public:
    Config(const double dist_threshold, const double normal_threshold, const double truncationDistance,
            const Eigen::Vector3d volumeOrigin,const int x,const int y, const int z, const double voxelScale):
    m_dist_threshold(dist_threshold),
    m_normal_threshold(normal_threshold),
    m_truncationDistance(truncationDistance),
    m_voxelScale(voxelScale),
    m_volumeSize(x,y,z),
    m_volumeOrigin(volumeOrigin)
    {};

    const double m_dist_threshold;
    const double m_normal_threshold;
    const double m_truncationDistance;
    const double m_voxelScale;
    Eigen::Vector3i m_volumeSize;
    const Eigen::Vector3d m_volumeOrigin;

};

bool process_frame( size_t frame_cnt, std::shared_ptr<Frame> prevFrame,std::shared_ptr<Frame> currentFrame, std::shared_ptr<Volume> volume,const Config& config)
{
    // STEP 1: estimate Pose
    icp icp(config.m_dist_threshold,config.m_normal_threshold);

    Eigen::Matrix4d estimated_pose = prevFrame->getGlobalPose();
    currentFrame->setGlobalPose(estimated_pose);

    std::cout << "Init: ICP..." << std::endl;
    if(!icp.estimatePose(frame_cnt, prevFrame,currentFrame, 3, estimated_pose)){
        throw "ICP Pose Estimation failed";
    };

    // STEP 2: Surface reconstruction
   std::cout << "Init: Fusion..." << std::endl;
    if(!fusion.reconstructSurface(currentFrame,volume, config.m_truncationDistance)){
        throw "Surface reconstruction failed";
    };

   std::cout << "Init: Raycast..." << std::endl;
    if(!raycast.surfacePrediction(currentFrame,volume, config.m_truncationDistance)){
        throw "Raycasting failed";
    };
    std::cout << "Done!" << std::endl;
    return true;
}

int main(){

    std::string filenameIn = PROJECT_DATA_DIR + std::string("/rgbd_dataset_freiburg1_xyz/");

    std::cout << "Initialize virtual sensor..." << std::endl;

    //KinectVirtualSensor sensor;
    VirtualSensor sensor;

    //std::string filenameIn = PROJECT_DATA_DIR + std::string("/rs10/");

    if (!sensor.init(filenameIn)) {
        std::cout << "Failed to initialize the sensor!\nCheck file path!" << std::endl;
        return -1;
    }

    // rec.record(20);

    // We store a first frame as a reference frame. All next frames are tracked relatively to the first frame.
    sensor.processNextFrame();

    //TODO truncationDistance is completly random Value right now
    Eigen::Vector3d volumeRange(5.0, 5.0, 5.0);
    Eigen::Vector3i volumeSize (50,50,50);
    double voxelSize = volumeRange.x()/volumeSize.x();

    const auto volumeOrigin = Eigen::Vector3d (-volumeRange.x()/2,-volumeRange.y()/2,0.5);

    Config config (0.1,0.5,0.5, volumeOrigin, volumeSize.x(),volumeSize.y(),volumeSize.z(), voxelSize);

    //Setup Volume
    auto volume = std::make_shared<Volume>(config.m_volumeOrigin, config.m_volumeSize,config.m_voxelScale) ;

    Eigen::Matrix3d depthIntrinsics = sensor.getDepthIntrinsics();
    Eigen::Matrix3d colIntrinsics   = sensor.getColorIntrinsics();
    Eigen::Matrix4d d2cExtrinsics   = sensor.getD2CExtrinsics();
    const unsigned int depthWidth         = sensor.getDepthImageWidth();
    const unsigned int depthHeight        = sensor.getDepthImageHeight();

    const double* depthMap = &sensor.getDepth()[0];
    BYTE* colors = &sensor.getColorRGBX()[0];

    std::shared_ptr<Frame> prevFrame = std::make_shared<Frame>(Frame(depthMap, colors, depthIntrinsics, colIntrinsics, d2cExtrinsics, depthWidth, depthHeight));

    int i = 1;
    const int iMax = 60;

    while(sensor.processNextFrame() && i <= iMax){

        const double* depthMap = &sensor.getDepth()[0];
        BYTE* colors = &sensor.getColorRGBX()[0];
        std::shared_ptr<Frame> currentFrame = std::make_shared<Frame>(Frame(depthMap, colors, depthIntrinsics,colIntrinsics, d2cExtrinsics, depthWidth, depthHeight));
        process_frame(i,prevFrame,currentFrame,volume,config);

        std::stringstream filename;
        filename << "mesh" << i;
        MeshWriter::toFile(filename.str(),currentFrame);

        prevFrame = std::move(currentFrame);
        i++;
    }

    MeshWriter::toFile("volume_mesh",*volume);
}

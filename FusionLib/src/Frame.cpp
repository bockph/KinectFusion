#include "Frame.h"
#include "MeshWriter.h"

#include <iostream>


Frame::Frame(const double * depthMap, const BYTE* colorMap,
        const Eigen::Matrix3d &depthIntrinsics, const Eigen::Matrix3d &colorIntrinsics,
        const Eigen::Matrix4d &d2cExtrinsics,
        const unsigned int width, const unsigned int height, double maxDistance)
        : m_width(width),m_height(height),m_intrinsic_matrix(depthIntrinsics), m_color_intrinsic_matrix(colorIntrinsics),
        m_d2cExtrinsics(d2cExtrinsics), m_maxDistance(maxDistance)
        {

    double depth_threshold = 6.;
    m_depth_map = std::vector<double>(width*height);
    for (size_t i = 0; i < width*height; ++i) {
        if(depthMap[i] < depth_threshold) {
            m_depth_map[i] = depthMap[i];
        }
    }

    auto pointsTmp = computeCameraCoordinates(width, height);

    std::vector<Vector4uc> cols (width*height);
    for (size_t i = 0; i < width*height; i++)
        cols[i] = Vector4uc(colorMap[i*4],colorMap[i*4+1],colorMap[i*4+2],colorMap[i*4+3]);

    auto normalsTmp = computeNormals(pointsTmp, width, height, maxDistance);
    addValidPoints(pointsTmp, normalsTmp);
    setGlobalPose(Eigen::Matrix4d::Identity());

    alignColorsToDepth(cols);

}

void Frame::alignColorsToDepth(std::vector<Vector4uc> colors) {
    const auto rotation = m_d2cExtrinsics.block(0,0,3,3);
    const auto translation = m_d2cExtrinsics.block(0,3,3,1);

    BYTE zero = (BYTE) 255;

    m_color_map.reserve(m_points.size());
    for( size_t i = 0; i < m_points.size(); ++i){
        Eigen::Vector2i coord = projectOntoColorPlane( rotation* m_points[i] + translation);
        if(contains(coord))
            m_color_map.push_back(colors[coord.x() + coord.y()*m_width]);
        else
            m_color_map.push_back(Vector4uc(zero, zero, zero, zero));
    }
}


Eigen::Vector3d Frame::projectIntoCamera(const Eigen::Vector3d& globalCoord){
    Eigen::Matrix4d pose_inverse = m_global_pose.inverse();
    const auto rotation_inv = pose_inverse.block(0,0,3,3);
    const auto translation_inv = pose_inverse.block(0,3,3,1);
    return rotation_inv * globalCoord + translation_inv;
}

bool Frame::contains(const Eigen::Vector2i& img_coord){
    return img_coord[0] < m_width && img_coord[1] < m_height && img_coord[0] >= 0 && img_coord[1] >= 0;
}


Eigen::Vector2i Frame::projectOntoPlane(const Eigen::Vector3d &cameraCoord, Eigen::Matrix3d& intrinsics){
    Eigen::Vector3d projected = (intrinsics*cameraCoord);
    if(projected[2] == 0){
        return Eigen::Vector2i(MINF, MINF);
    }
    projected /= projected[2];
    return (Eigen::Vector2i ((int) round(projected.x()), (int)round(projected.y())));
}
Eigen::Vector2i Frame::projectOntoDepthPlane(const Eigen::Vector3d &cameraCoord){
    return projectOntoPlane(cameraCoord, m_intrinsic_matrix);
}

Eigen::Vector2i Frame::projectOntoColorPlane(const Eigen::Vector3d& cameraCoord){
    return projectOntoPlane(cameraCoord, m_color_intrinsic_matrix);
}

void Frame::addValidPoints(std::vector<Eigen::Vector3d> points, std::vector<Eigen::Vector3d> normals)
{

    // We filter out measurements where either point or normal is invalid.
    const unsigned nPoints = points.size();
    m_points.reserve(nPoints);
    m_normals.reserve(nPoints);

    for (size_t i = 0; i < nPoints; i++) {
        const auto& point = points[i];
        const auto& normal = normals[i];

        if ( (point.allFinite() && normal.allFinite()) ) {
            m_points.push_back(point);
            m_normals.push_back(normal);
        }
        else{
            //m_depth_map[i] = MINF;
            m_points.emplace_back(Eigen::Vector3d(MINF, MINF, MINF));
            m_normals.emplace_back(Eigen::Vector3d(MINF, MINF, MINF));
        }
    }
}

std::vector<Eigen::Vector3d> Frame::computeCameraCoordinates(unsigned int width, unsigned int height){
    double fovX = m_intrinsic_matrix(0, 0);
    double fovY = m_intrinsic_matrix(1, 1);
    double cX = m_intrinsic_matrix(0, 2);
    double cY = m_intrinsic_matrix(1, 2);

    // Back-project the pixel depths into the camera space.
    std::vector<Eigen::Vector3d> pointsTmp(width * height);

    for (size_t y = 0; y < height; ++y){
        for (size_t x = 0; x < width; ++x){
            unsigned int idx = x + (y * width);
            double depth = m_depth_map[idx];

            if (depth == MINF) {
                pointsTmp[idx] = Eigen::Vector3d(MINF, MINF, MINF);
            }
            else {
                // Back-projection to camera space.
                pointsTmp[idx] = Eigen::Vector3d((x - cX) / fovX * depth, (y - cY) / fovY * depth, depth);
            }
        }
    }
    return pointsTmp;
}

void Frame::computeNormalFromGlobals(){
    std::vector<Eigen::Vector3d> camera_points;
    for(const auto& global: m_points_global){
        camera_points.emplace_back(projectIntoCamera(global));
    }
    m_normals_global = computeNormals(camera_points, m_width, m_height, m_maxDistance);
}

std::vector<Eigen::Vector3d> Frame::computeNormals(std::vector<Eigen::Vector3d> camera_points, unsigned int width, unsigned int height,  double maxDistance){

    // We need to compute derivatives and then the normalized normal vector (for valid pixels).
    std::vector<Eigen::Vector3d> normalsTmp(width * height);

    for (size_t v = 1; v < height - 1; ++v) {
        for (size_t u = 1; u < width - 1; ++u) {
            unsigned int idx = v*width + u; // linearized index

            const Eigen::Vector3d du = camera_points[idx + 1] - camera_points[idx - 1];
            const Eigen::Vector3d dv = camera_points[idx + width] - camera_points[idx - width];

            if (!du.allFinite() || !dv.allFinite()
                    || du.norm() > maxDistance
                    || dv.norm() > maxDistance) {
                normalsTmp[idx] = Eigen::Vector3d(MINF, MINF, MINF);
                continue;
            }

            normalsTmp[idx] = du.cross(dv);
            normalsTmp[idx].normalize();
        }
    }

    // We set invalid normals for border regions.
    for (size_t u = 0; u < width; ++u) {
        normalsTmp[u] = Eigen::Vector3d(MINF, MINF, MINF);
        normalsTmp[u + (height - 1) * width] = Eigen::Vector3d(MINF, MINF, MINF);
    }
    for (size_t v = 0; v < height; ++v) {
        normalsTmp[v * width] = Eigen::Vector3d(MINF, MINF, MINF);
        normalsTmp[(width - 1) + v * width] = Eigen::Vector3d(MINF, MINF, MINF);
    }
    return normalsTmp;
}

void Frame::applyGlobalPose(Eigen::Matrix4d& estimated_pose){
    Eigen::Matrix3d rotation = estimated_pose.block(0,0,3,3);

    m_points_global  = transformPoints(m_points, estimated_pose);
    m_normals_global = rotatePoints(m_normals, rotation);
}

std::vector<Eigen::Vector3d> Frame::transformPoints(std::vector<Eigen::Vector3d>& points, Eigen::Matrix4d& transformation){
    const Eigen::Matrix3d rotation = transformation.block(0,0,3,3);
    const Eigen::Vector3d translation = transformation.block(0,3,3,1);
    std::vector<Eigen::Vector3d> transformed (points.size());

    for( size_t idx = 0; idx < points.size(); ++idx){
        if(points[idx].allFinite())
            transformed[idx] = rotation * points[idx] + translation;
        else
            transformed[idx] = (Eigen::Vector3d(MINF, MINF, MINF));
    }
    return transformed;
}

std::vector<Eigen::Vector3d> Frame::rotatePoints(std::vector<Eigen::Vector3d>& points, Eigen::Matrix3d& rotation){
    std::vector<Eigen::Vector3d> transformed (points.size());

    for( size_t idx = 0; idx < points.size(); ++idx){
        if(points[idx].allFinite())
            transformed[idx] = rotation * points[idx];
        else
            transformed[idx] = (Eigen::Vector3d(MINF, MINF, MINF));
    }
    return transformed;
}

const std::vector<Eigen::Vector3d>& Frame::getPoints() const {
    return m_points;
}

const std::vector<Eigen::Vector3d>& Frame::getNormals() const {
    return m_normals;
}

const std::vector<Eigen::Vector3d>& Frame::getGlobalNormals() const{
    return m_normals_global;
}

void Frame::setGlobalNormal(const Eigen::Vector3d& normal, size_t u, size_t v){
    size_t idx = v * m_width + u;
    m_normals_global[idx] = normal;
}

const std::vector<Eigen::Vector3d>& Frame::getGlobalPoints() const{
    return m_points_global;
}

void Frame::setGlobalPoint(const Eigen::Vector3d& point, size_t u, size_t v){
    size_t idx = v * m_width + u;
    m_points_global[idx] = point;
}

const Eigen::Matrix4d& Frame::getGlobalPose() const{
    return m_global_pose;
}

void Frame::setGlobalPose(const Eigen::Matrix4d& pose) {
    m_global_pose = pose;
    applyGlobalPose(m_global_pose);
}

const std::vector<double>& Frame::getDepthMap() const{
    return m_depth_map;
}

const std::vector<Vector4uc>& Frame::getColorMap() const{
    return m_color_map;
}

void Frame::setColor(const Vector4uc& color, size_t u, size_t v){
    size_t idx = v * m_width + u;
    m_color_map[idx] = color;
}

const Eigen::Matrix3d& Frame::getIntrinsics() const{
    return m_intrinsic_matrix;
}

const unsigned int Frame::getWidth() const{
    return m_width;
}

const unsigned int Frame:: getHeight() const{
    return m_height;
}

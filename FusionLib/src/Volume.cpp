#include "Volume.hpp"

Ray::Ray(const Eigen::Vector3d &origin, const Eigen::Vector3d &dir) : orig(origin), dir(dir) {
    invdir[0] = 1/dir[0];
    invdir[1] = 1/dir[1];
    invdir[2] = 1/dir[2];
    sign[0] = (invdir.x() < 0);
    sign[1] = (invdir.y() < 0);
    sign[2] = (invdir.z() < 0);
}


Volume::Volume(const Eigen::Vector3d origin, const Eigen::Vector3i volumeSize, const double voxelScale)
        : _voxelData(), _volumeSize(volumeSize),
          _voxelScale(voxelScale), _origin(origin),
          _maxPoint(voxelScale * volumeSize.cast<double>()),
          _volumeRange(volumeSize.cast<double>()*voxelScale)
                  {
    _voxelData.reserve(volumeSize.x() * volumeSize.y() * volumeSize.z());
    for (int z = 0;z<volumeSize.z();z++)
        for( int y =0;y<volumeSize.y();y++)
            for(int x=0;x< volumeSize.x();x++)
                _voxelData.emplace_back(Voxel());

    Eigen::Vector3d half_voxelSize(voxelScale/2, voxelScale/2, voxelScale/2);
    bounds[0] = _origin + half_voxelSize;
    bounds[1] = _maxPoint - half_voxelSize;
}

bool Volume::intersects(const Ray &r, double& entry_distance) const{

    float tmin, tmax, tymin, tymax, tzmin, tzmax;

    tmin = (bounds[r.sign[0]].x() - r.orig.x()) * r.invdir.x();
    tmax = (bounds[1-r.sign[0]].x() - r.orig.x()) * r.invdir.x();
    tymin = (bounds[r.sign[1]].y() - r.orig.y()) * r.invdir.y();
    tymax = (bounds[1-r.sign[1]].y() - r.orig.y()) * r.invdir.y();

    if ((tmin > tymax) || (tymin > tmax))
        return false;
    if (tymin > tmin)
        tmin = tymin;
    if (tymax < tmax)
        tmax = tymax;

    tzmin = (bounds[r.sign[2]].z() - r.orig.z()) * r.invdir.z();
    tzmax = (bounds[1-r.sign[2]].z() - r.orig.z()) * r.invdir.z();

    if ((tmin > tzmax) || (tzmin > tmax))
        return false;
    if (tzmin > tmin)
        tmin = tzmin;
    if (tzmax < tmax)
        tmax = tzmax;

    entry_distance = tmin;

    //double z_entry = r.dir.z() == 0 ? 0 : tzmin;// tzmin*r.dir.z();
    //double x_entry = r.dir.x() == 0 ? 0 : tmin;// tmin*r.dir.x();
    //double y_entry = r.dir.y() == 0 ? 0 : tymin;// tymin*r.dir.y();
    // entry_distance = Eigen::Vector3d(tmin*r.dir.x(), tymin*r.dir.y(), );
    // entry_distance = Eigen::Vector3d( x_entry, y_entry, z_entry );
    //Eigen::Vector3d intersecting_voxel = (intersection - _origin) / _voxelScale;
    // voxel[0] = (int) intersecting_voxel[0];
    // voxel[1] = (int) intersecting_voxel[1];
    // voxel[2] = (int) intersecting_voxel[2];

    return true;
}

const Eigen::Vector3d& Volume::getOrigin() const{
    return _origin;
}


std::vector<Voxel>& Volume::getVoxelData()  {
    return _voxelData;
}

const Eigen::Vector3i &Volume::getVolumeSize() const {
    return _volumeSize;
}

float Volume::getVoxelScale() const {
    return _voxelScale;
}

bool Volume::contains(const Eigen::Vector3d global_point){
    Eigen::Vector3d volumeCoord = (global_point - _origin);

    return (volumeCoord.x() < 1 || volumeCoord.x() >= _volumeRange.x() - 1 || volumeCoord.y() < 1 ||
            volumeCoord.y() >= _volumeRange.y() - 1 ||
            volumeCoord.z() < 1 || volumeCoord.z() >= _volumeRange.z() - 1);

}

double Volume::getTSDF(Eigen::Vector3d global){
    Eigen::Vector3d shifted = (global - _origin) / _voxelScale;
    Eigen::Vector3i currentPosition;
    currentPosition.x() = int(shifted.x());
    currentPosition.y() = int(shifted.y());
    currentPosition.z() = int(shifted.z());

    Voxel voxelData = getVoxelData()[currentPosition.x() + currentPosition.y()*_volumeSize.x()
                                                               + currentPosition.z()*_volumeSize.x()*_volumeSize.y()];
    return voxelData.tsdf;
}

Eigen::Vector3d Volume::getTSDFGrad(Eigen::Vector3d global){
    Eigen::Vector3d shifted = (global - _origin) / _voxelScale;
    Eigen::Vector3i currentPosition;
    currentPosition.x() = int(shifted.x());
    currentPosition.y() = int(shifted.y());
    currentPosition.z() = int(shifted.z());

    // TODO: double check

    double tsdf_x0 = getVoxelData()[(currentPosition.x()-1) + currentPosition.y()*_volumeSize.x()
                                                               + currentPosition.z()*_volumeSize.x()*_volumeSize.y()].tsdf;
    double tsdf_x1 = getVoxelData()[(currentPosition.x()+1) + currentPosition.y()*_volumeSize.x()
                                                               + currentPosition.z()*_volumeSize.x()*_volumeSize.y()].tsdf;
    double tsdf_y0 = getVoxelData()[currentPosition.x() + (currentPosition.y()-1)*_volumeSize.x()
                                                               + currentPosition.z()*_volumeSize.x()*_volumeSize.y()].tsdf;
    double tsdf_y1 = getVoxelData()[currentPosition.x() + (currentPosition.y()+1)*_volumeSize.x()
                                                               + currentPosition.z()*_volumeSize.x()*_volumeSize.y()].tsdf;
    double tsdf_z0 = getVoxelData()[currentPosition.x() + currentPosition.y()*_volumeSize.x()
                                                               + (currentPosition.z()-1)*_volumeSize.x()*_volumeSize.y()].tsdf;
    double tsdf_z1 = getVoxelData()[currentPosition.x() + currentPosition.y()*_volumeSize.x()
                                                               + (currentPosition.z()+1)*_volumeSize.x()*_volumeSize.y()].tsdf;

    return Eigen::Vector3d(tsdf_x1 - tsdf_x0, tsdf_y1 - tsdf_y0, tsdf_z1 - tsdf_z0) / (_voxelScale*2);
}

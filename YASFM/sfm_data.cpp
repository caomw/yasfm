/*
* Filip Srajer
* filip.srajer (at) fel.cvut.cz
* Center for Machine Perception
* Czech Technical University in Prague
*
* This software is under construction.
* 10/2015
*/
#include "sfm_data.h"

#include <iostream>

using Eigen::ArrayXf;
using Eigen::Map;
using std::cerr;
using std::cout;

namespace yasfm
{

Camera::Camera(const string& imgFilename)
  : imgWidth_(-1),imgHeight_(-1),imgFilename_(imgFilename)
{
  imgFilename_ = imgFilename;
  getImgDims(imgFilename,&imgWidth_,&imgHeight_);
}
Camera::~Camera()
{
}

void Camera::reserveFeatures(int num,int dim)
{
  keys_.reserve(num);
  descr_.resize(dim,num);
}

void Camera::addFeature(double x,double y,const float* const descr)
{
  auto idx = keys_.size();
  keys_.emplace_back(x,y);
  Map<const ArrayXf> descrMapped(descr,descr_.rows());
  descr_.col(idx) = descrMapped;
}
void Camera::clearDescriptors()
{
  descr_.resize(0,0);
}

const string& Camera::imgFilename() const { return imgFilename_; }
int Camera::imgWidth() const { return imgWidth_; }
int Camera::imgHeight() const { return imgHeight_; }
const vector<Vector2d>& Camera::keys() const { return keys_; }
const Vector2d& Camera::key(int i) const { return keys_[i]; }
const ArrayXXf& Camera::descr() const { return descr_; }

unique_ptr<Camera> Camera::clone() const
{
  return make_unique<Camera>(*this);
}

StandardCamera::StandardCamera(const string& imgFilename)
  : Camera(imgFilename),C_(Vector3d::Zero()),f_(0),
  paramsConstraints_(nParams_,0.),paramsConstraintsWeights_(nParams_,0.)
{
  // assume the image center to be the principal point
  x0_(0) = 0.5 * (imgWidth() - 1);
  x0_(1) = 0.5 * (imgHeight() - 1);
}
StandardCamera::~StandardCamera()
{

}
unique_ptr<Camera> StandardCamera::clone() const
{
  return make_unique<StandardCamera>(*this);
}

void StandardCamera::setFocal(double f)
{
  f_ = f;
}
void StandardCamera::constrainFocal(double constraint,double weigtht)
{
  paramsConstraints_[fIdx_] = constraint;
  paramsConstraintsWeights_[fIdx_] = weigtht;
}
Vector2d StandardCamera::project(const Vector3d& pt) const
{
  Vector2d ptCam = (rot_ * (pt - C_)).hnormalized();
  return f_ * ptCam + x0_;
}
void StandardCamera::setParams(const Matrix34d& P)
{
  Matrix3d K,R;
  Vector3d Ctmp;
  P2KRC(P,&K,&R,&Ctmp);

  setFocal(0.5*(K(0,0)+K(1,1)));
  C_ = Ctmp;
  rot_.fromRotationMatrix(R);
}
void StandardCamera::setRotation(const Matrix3d& R)
{
  rot_.fromRotationMatrix(R);
}
void StandardCamera::setC(const Vector3d& C)
{
  C_ = C;
}

Matrix34d StandardCamera::P() const
{
  Matrix34d out(Matrix34d::Identity());
  out.rightCols(1) = -C_;
  out = K()*rot_.toRotationMatrix()*out;
  return out;
}
Vector2d StandardCamera::keyNormalized(int i) const
{
  return (key(i) - x0_) / f_;
}
Matrix3d StandardCamera::K() const
{
  Matrix3d K(Matrix3d::Identity());
  K(0,0) = f();
  K(1,1) = f();
  K.block(0,2,2,1) = x0_;
  return K;
}
Matrix34d StandardCamera::pose() const
{
  Matrix34d tmp(Matrix34d::Identity());
  tmp.col(3) = -C_;
  return R() * tmp;
}
Matrix3d StandardCamera::R() const
{
  return rot_.toRotationMatrix();
}

void StandardCamera::params(vector<double> *pparams) const
{
  auto& params = *pparams;
  params.resize(nParams_);
  Map<Vector3d> rot(&params[rotIdx_]),C(&params[CIdx_]);
  rot = rot_.angle() * rot_.axis();
  C = C_;
  params[fIdx_] = f_;
}
void StandardCamera::setParams(const vector<double>& params)
{
  Map<const Vector3d> rot(&params[rotIdx_]),C(&params[CIdx_]);
  double angle = rot.squaredNorm();
  Vector3d axis;
  if(angle == 0.)
    axis = Vector3d::UnitX();
  else
  {
    angle = sqrt(angle);
    axis = rot / angle;
  }
  rot_ = AngleAxisd(angle,axis);
  C_ = C;
  f_ = params[fIdx_];
}
StandardCamera::ReprojectionErrorFunctor::ReprojectionErrorFunctor(double keyX,
  double keyY,const StandardCamera& cam)
  : keyX_(keyX),keyY_(keyY),cam_(cam)
{
}
ceres::CostFunction* StandardCamera::costFunction(int keyIdx) const
{
  const auto& k = key(keyIdx);
  return (new ceres::AutoDiffCostFunction<ReprojectionErrorFunctor,2,nParams_,3>(
    new ReprojectionErrorFunctor(k(0),k(1),*this)));
}
ceres::CostFunction* StandardCamera::constraintsCostFunction() const
{
  return generateConstraintsCostFunction<nParams_>(&paramsConstraints_[0],
    &paramsConstraintsWeights_[0]);
}

void StandardCamera::setParamsConstraints(const vector<double>& constraints,
  const vector<double>& weights)
{
  for(size_t i = 0; i < paramsConstraints_.size(); i++)
  {
    paramsConstraints_[i] = constraints[i];
    paramsConstraintsWeights_[i] = weights[i];
  }
}

Vector3d StandardCamera::C() const { return C_; }

const AngleAxisd& StandardCamera::rot() const { return rot_; }
double StandardCamera::f() const { return f_; }
const Vector2d& StandardCamera::x0() const { return x0_; }

StandardCameraRadial::StandardCameraRadial(const string& imgFilename)
  : StandardCamera(imgFilename)
{
  paramsConstraints_.push_back(0.);
  paramsConstraints_.push_back(0.);
  paramsConstraintsWeights_.push_back(0.);
  paramsConstraintsWeights_.push_back(0.); 
  radParams_[0] = 0.;
  radParams_[1] = 0.;
  invRadParams_.fill(0.);
}
StandardCameraRadial::~StandardCameraRadial()
{
}
unique_ptr<Camera> StandardCameraRadial::clone() const
{
  return make_unique<StandardCameraRadial>(*this);
}
void StandardCameraRadial::constrainRadial(double *constraints,double *weigthts)
{
  paramsConstraints_[radIdx_ + 0] = constraints[0];
  paramsConstraints_[radIdx_ + 1] = constraints[1];
  paramsConstraintsWeights_[radIdx_ + 0] = weigthts[0];
  paramsConstraintsWeights_[radIdx_ + 1] = weigthts[1];
}
Vector2d StandardCameraRadial::project(const Vector3d& pt) const
{
  Vector2d ptCam = (rot_ * (pt - C_)).hnormalized();
  double r2 = ptCam.squaredNorm();
  double distortion = 1. + r2 * (radParams_[0] + r2 * radParams_[1]);
  return f_ * distortion * ptCam + x0_;
}
Vector2d StandardCameraRadial::keyNormalized(int i) const
{
  Vector2d distorted = StandardCamera::keyNormalized(i);
  double radius = distorted.norm();
  double undistortFactor = 1. + radius *
    (invRadParams_[0] + radius *
      (invRadParams_[1] + radius *
        (invRadParams_[2] + radius * invRadParams_[3])));
  return undistortFactor*distorted;
}
void StandardCameraRadial::setParams(const Matrix34d& P)
{
  StandardCamera::setParams(P);
  radParams_[0] = 0.;
  radParams_[1] = 0.;
  invRadParams_.fill(0.);
}

void StandardCameraRadial::params(vector<double> *pparams) const
{
  auto& params = *pparams;
  StandardCamera::params(pparams);
  params.push_back(radParams_[0]);
  params.push_back(radParams_[1]);
}
void StandardCameraRadial::setParams(const vector<double>& params)
{
  StandardCamera::setParams(params);
  radParams_[0] = params[radIdx_ + 0];
  radParams_[1] = params[radIdx_ + 1];

  // update inverse parameters
  array<double,4> radParamsFull = {0.,radParams_[0],0.,radParams_[1]};
  double xMax = imgWidth() - x0_(0);
  double yMax = imgHeight() - x0_(1);
  double maxRadius = sqrt(xMax*xMax + yMax*yMax);
  int nForward = static_cast<int>(radParamsFull.size());
  int nInverse = static_cast<int>(invRadParams_.size());
  approximateInverseRadialDistortion(nForward,nInverse,maxRadius,&radParamsFull[0],
    &invRadParams_[0]);
}
StandardCameraRadial::ReprojectionErrorFunctor::ReprojectionErrorFunctor(double keyX,
  double keyY,const StandardCameraRadial& cam)
  : keyX_(keyX),keyY_(keyY),cam_(cam)
{
}
ceres::CostFunction* StandardCameraRadial::costFunction(int keyIdx) const
{
  const auto& k = key(keyIdx);
  return (new ceres::AutoDiffCostFunction<ReprojectionErrorFunctor,2,nParams_,3>(
    new ReprojectionErrorFunctor(k(0),k(1),*this)));
}
ceres::CostFunction* StandardCameraRadial::constraintsCostFunction() const
{
  return generateConstraintsCostFunction<nParams_>(&paramsConstraints_[0],
    &paramsConstraintsWeights_[0]);
}

const double* const StandardCameraRadial::radParams() const { return &radParams_[0]; }


void Points::addPoints(const IntPair& camsIdxs,const vector<int>& matchesToReconstructIdxs,
  const vector<Vector3d>& coord)
{
  size_t sz = ptCoord_.size() + coord.size();
  ptCoord_.reserve(sz);
  ptData_.reserve(sz);

  for(size_t i = 0; i < coord.size(); i++)
  {
    ptCoord_.push_back(coord[i]);
    ptData_.emplace_back();
    const auto& nViewMatch = matchesToReconstruct_[matchesToReconstructIdxs[i]];

    auto& reconstructed = ptData_.back().reconstructed;
    reconstructed[camsIdxs.first] = nViewMatch.at(camsIdxs.first);
    reconstructed[camsIdxs.second] = nViewMatch.at(camsIdxs.second);
    auto& toReconstruct = ptData_.back().toReconstruct;
    toReconstruct = nViewMatch;
    toReconstruct.erase(camsIdxs.first);
    toReconstruct.erase(camsIdxs.second);
  }
  filterOutOutliers(matchesToReconstructIdxs,&matchesToReconstruct_);
}
void Points::addPoints(const vector<Vector3d>& pointCoord,
  const vector<SplitNViewMatch>& pointViews)
{
  ptCoord_.reserve(numPts() + pointCoord.size());
  ptData_.reserve(numPts() + pointCoord.size());
  for(size_t i = 0; i < pointCoord.size(); i++)
  {
    ptCoord_.push_back(pointCoord[i]);
    ptData_.emplace_back();
    ptData_.back().reconstructed = pointViews[i].observedPart;
    ptData_.back().toReconstruct = pointViews[i].unobservedPart;
  }
}

void Points::removePoints(const vector<bool>& keep)
{
  filterVector(keep,&ptCoord_);
  filterVector(keep,&ptData_);
}
int Points::numPts() const 
{
  return static_cast<int>(ptCoord().size());
}
void Points::markCamAsReconstructed(int camIdx)
{
  for(auto& entry : ptData_)
  {
    entry.reconstructed.emplace(camIdx,entry.toReconstruct.at(camIdx));
    entry.toReconstruct.erase(camIdx);
  }
}
void Points::markCamAsReconstructed(int camIdx,
  const vector<int>& correspondingPoints,
  const vector<int>& correspondingPointsInliers)
{
  for(int inlierIdx : correspondingPointsInliers)
  {
    auto& entry = ptData_[correspondingPoints[inlierIdx]];
    entry.reconstructed.emplace(camIdx,entry.toReconstruct.at(camIdx));
  }
  for(int ptIdx : correspondingPoints)
  {
    auto& entry = ptData_[ptIdx];
    entry.toReconstruct.erase(camIdx);
  }
}

const vector<NViewMatch>& Points::matchesToReconstruct() const { return matchesToReconstruct_; }
vector<NViewMatch>& Points::matchesToReconstruct() { return matchesToReconstruct_; }
const vector<Vector3d>& Points::ptCoord() const { return ptCoord_; }
double* Points::ptCoord(int ptIdx)
{
  return &ptCoord_[ptIdx](0);
}
const vector<Points::PointData>& Points::ptData() const { return ptData_; }

Dataset::Dataset(const string& dir)
  : dir_(dir)
{
}

Dataset::Dataset(const Dataset& o)
{
  copyIn(o);
}

Dataset& Dataset::operator = (const Dataset& o)
{
  copyIn(o);
  return *this;
}
void Dataset::copyIn(const Dataset& o)
{
  dir_ = o.dir_;
  cams_.reserve(o.cams_.size());
  for(const auto& cam : o.cams_)
  {
    cams_.push_back(cam->clone());
  }
  pairs_ = o.pairs_;
  reconstructedCams_ = o.reconstructedCams_;
  points_ = o.points_;
}
void Dataset::clearDescriptors()
{
  for(auto& cam : cams_)
  {
    cam->clearDescriptors();
  }
}
void Dataset::markCamAsReconstructed(int camIdx)
{
  reconstructedCams_.insert(camIdx);
  points_.markCamAsReconstructed(camIdx);
}
void Dataset::markCamAsReconstructed(int camIdx,
  const vector<int>& correspondingPoints,
  const vector<int>& correspondingPointsInliers)
{
  reconstructedCams_.insert(camIdx);
  points_.markCamAsReconstructed(camIdx,correspondingPoints,
    correspondingPointsInliers);
}

int Dataset::numCams() const
{
  return static_cast<int>(cams_.size());
}

const string& Dataset::dir() const { return dir_; }
const Camera& Dataset::cam(int idx) const { return *cams_[idx]; }
const Camera& Dataset::cam(size_t idx) const { return *cams_[idx]; }
Camera& Dataset::cam(int idx) { return *cams_[idx]; }
Camera& Dataset::cam(size_t idx) { return *cams_[idx]; }
const ptr_vector<Camera>& Dataset::cams() const { return cams_; }
ptr_vector<Camera>& Dataset::cams() { return cams_; }
const pair_umap<CameraPair>& Dataset::pairs() const { return pairs_; }
pair_umap<CameraPair>& Dataset::pairs() { return pairs_; }
const uset<int>& Dataset::reconstructedCams() const { return reconstructedCams_; }
const Points& Dataset::points() const { return points_; }
Points& Dataset::points() { return points_; }

} // namespace yasfm
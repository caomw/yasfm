#include "features.h"

#include <iostream>
#include <string>
#include <vector>
#include <memory>

using std::cerr;
using std::cout;
using std::string;
using std::to_string;
using std::vector;
using std::unique_ptr;
using namespace yasfm;

namespace yasfm
{

bool OptionsSIFTGPU::isSetMaxWorkingDimension() const { return maxWorkingDimension >= 0; }
bool OptionsSIFTGPU::isSetMaxOctaves() const { return maxOctaves >= 0; }
bool OptionsSIFTGPU::isSetDogLevelsInAnOctave() const { return dogLevelsInAnOctave >= 0; }
bool OptionsSIFTGPU::isSetDogThresh() const { return dogThresh >= 0; }
bool OptionsSIFTGPU::isSetEdgeThresh() const { return edgeThresh >= 0; }

void OptionsSIFTGPU::write(ostream& file) const
{
  file << " maxWorkingDimension: " << maxWorkingDimension << "\n";
  file << " firstOctave: " << firstOctave << "\n";
  file << " maxOctaves: " << maxOctaves << "\n";
  file << " dogLevelsInAnOctave: " << dogLevelsInAnOctave << "\n";
  file << " dogThresh: " << dogThresh << "\n";
  file << " edgeThresh: " << edgeThresh << "\n";
  file << " detectUprightSIFT: " << detectUprightSIFT << "\n";
  file << " verbosityLevel: " << verbosityLevel << "\n";
}

void detectSiftGPU(const OptionsSIFTGPU& opt, ptr_vector<Camera> *cams, 
	DetectSiftCallbackFunctionPtr callbackFunction, void * callbackObjectPtr)
{
  int maxWidth = -1;
  int maxHeight = -1;
  for(const auto& pcam : (*cams))
  {
    maxWidth = std::max(maxWidth,pcam->imgWidth());
    maxHeight = std::max(maxHeight,pcam->imgHeight());
  }

  unique_ptr<SiftGPU> sift(CreateNewSiftGPU(1));

  bool success = initializeSiftGPU(opt,maxWidth,maxHeight,sift.get());
  if(!success)
  { return; }
  int done = 0;
  for(auto& pcam : (*cams))
  {
    ::detectSiftGPU(sift.get(),pcam.get());
	if (callbackFunction != NULL&&callbackObjectPtr != NULL)
	{
		callbackFunction(callbackObjectPtr, done);
	}
	done++;
  }
}

void detectSiftGPU(const OptionsSIFTGPU& opt,Camera *cam)
{
  unique_ptr<SiftGPU> sift(CreateNewSiftGPU(1));

  bool success = initializeSiftGPU(opt,cam->imgWidth(),cam->imgHeight(),sift.get());
  if(!success)
  {
    return;
  }
  
  ::detectSiftGPU(sift.get(),cam);
}

} // namespace yasfm


namespace
{

void detectSiftGPU(SiftGPU *sift,Camera *cam)
{
  if(sift->RunSIFT(cam->imgFilename().c_str()))
  {
    int num = sift->GetFeatureNum();
    auto *keys = new SiftGPU::SiftKeypoint[num];
    auto *descr = new float[128 * num];
    sift->GetFeatureVector(keys,descr);

    cam->resizeFeatures(num,128);
    for(int i = 0; i < num; i++)
    {
      cam->setFeature(i,keys[i].x,keys[i].y,keys[i].s,keys[i].o,descr + i * 128);
    }
    delete[] keys;
    delete[] descr;
  }
}

void setParamsSiftGPU(const OptionsSIFTGPU& opt,SiftGPU *sift)
{
  vector<string> opts;
  opts.push_back("-fo");
  opts.push_back(to_string(opt.firstOctave));
  if(opt.isSetMaxWorkingDimension())
  {
    opts.push_back("-maxd");
    opts.push_back(to_string(opt.maxWorkingDimension));
  }
  if(opt.isSetMaxOctaves())
  {
    opts.push_back("-no");
    opts.push_back(to_string(opt.maxOctaves));
  }
  if(opt.isSetDogLevelsInAnOctave())
  {
    opts.push_back("-d");
    opts.push_back(to_string(opt.dogLevelsInAnOctave));
  }
  if(opt.isSetDogThresh())
  {
    opts.push_back("-t");
    opts.push_back(to_string(opt.dogThresh));
  }
  if(opt.isSetEdgeThresh())
  {
    opts.push_back("-e");
    opts.push_back(to_string(opt.edgeThresh));
  }
  if(opt.detectUprightSIFT)
  {
    // fix orientation
    opts.push_back("-ofix");
    // max 1 orientation per feature
    opts.push_back("-m");
    opts.push_back("-mo");
    opts.push_back("1");
  }
  opts.push_back("-v");
  opts.push_back(to_string(opt.verbosityLevel));

  char **argv = new char*[opts.size()];
  for(size_t i = 0; i < opts.size(); i++)
  {
    argv[i] = new char[opts[i].length() + 1];
    strcpy(argv[i],opts[i].c_str());
  }
  int argc = (int)opts.size();
  sift->ParseParam(argc,argv);

  for(int i = 0; i < argc; i++)
  {
    delete[] argv[i];
  }
  delete[] argv;
}

bool initializeSiftGPU(const OptionsSIFTGPU& opt,int maxWidth,int maxHeight,
  SiftGPU *sift)
{
  setParamsSiftGPU(opt,sift);

  if(sift->CreateContextGL() != SiftGPU::SIFTGPU_FULL_SUPPORTED)
  {
    cerr << "ERROR: SiftGPUAutoMemRelease::initialize: Could not create OpenGL context.\n";
    return false;
  }

  sift->AllocatePyramid(maxWidth,maxHeight);
  return true;
}

} // namespace
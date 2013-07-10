/* 
 * Author: Victor Sand (victor.sand@gmail.com)
 *
 */

#include <Forge.h>
#include <BricksHeader.h>
#include <Brick.h>
#include <iostream>
#include <sstream>
#include <fstream>
#include <math.h>
#include <array>
#include <boost/filesystem.hpp>

using namespace osp;

Forge * Forge::New() {
  return new Forge();
}

Forge::Forge() 
  : inFilename_("NotSet"), outFilename_("NotSet"), header_(NULL),
    structure_(0), xBrickDim_(0), yBrickDim_(0), zBrickDim_(0), 
    paddingWidth_(0) {
}

Forge::~Forge() {
  if (header_) {
    delete header_;
  }
  for (auto it=bricks_.begin(); it!=bricks_.end(); ++it) {
    delete *it;
  }
}

void Forge::SetInFilename(std::string _inFilename) {
  inFilename_ = _inFilename;
}

void Forge::SetOutFilename(std::string _outFilename) {
  outFilename_ = _outFilename;
}

void Forge::SetStructure(unsigned int _structure) {
  structure_ = _structure;
}

void Forge::SetBrickDimensions(unsigned int _xBrickDim,
                               unsigned int _yBrickDim,
                               unsigned int _zBrickDim) {
  xBrickDim_ = _xBrickDim;
  yBrickDim_ = _yBrickDim;
  zBrickDim_ = _zBrickDim;
}

void Forge::SetPaddingWidth(unsigned int _paddingWidth) {
  paddingWidth_ = _paddingWidth;
}

bool Forge::Construct() {

  // Create header
  std::cout << "Creating header" << std::endl;
  if (!CreateHeader()) {
    std::cout << "Error: Could not create header" << std::endl;
    return false;
  }

  // Create one octree per timestep, save in temporary file
  if (!CreateOctree()) {
    std::cout << "Error: Failed to create temp octree file" << std::endl;
    return false;
  }

  // Use the octrees to construct a TSP tree, write directly to disk
  if (!ConstructTSPTree()) {
    std::cout << "Error: Failed to construct TSP tree" << std::endl;
    return false;
  }
  
  // Delete temporary file
  DeleteTempFile();

  return true;
}


bool Forge::CreateHeader() {

  std::cout << "Reading header" << std::endl;

  // Read header information
  if (header_) {
    std::cout << "Warning: Header already exists, deleting it!" << std::endl;
    delete header_;
  }

  header_ = BricksHeader::New();
  
  // Set the data not read from file
  header_->SetStructure(structure_);
  if (xBrickDim_ == 0 || yBrickDim_ == 0 || zBrickDim_ == 0) {
    std::cout << "Warning: One or more brick dimensions are zero!"<< std::endl;
  }
  header_->SetDimensions(xBrickDim_, yBrickDim_, zBrickDim_);
  header_->SetPaddingWidth(paddingWidth_);
  header_->SetDataSize(static_cast<unsigned int>(sizeof(real)));

  // Read from file
  if (instream_.is_open()) {
    std::cout << "Error: Instream is already open!" << std::endl;
    delete header_;
    return false;
  }
  instream_.open(inFilename_.c_str(), 

                std::ios_base::in | std::ios_base::binary);
  if (!instream_.is_open()) {
    std::cout << "Error: Could not open file." << std::endl;
    delete header_;
    return false;
  }
  
  unsigned int dataDimensionality = 0, numTimesteps = 0,
               xDim = 0, yDim = 0, zDim = 0;
  size_t s = sizeof(unsigned int);

  instream_.read(reinterpret_cast<char*>(&dataDimensionality), s);
  instream_.read(reinterpret_cast<char*>(&numTimesteps), s);
  instream_.read(reinterpret_cast<char*>(&xDim), s);
  instream_.read(reinterpret_cast<char*>(&yDim), s);
  instream_.read(reinterpret_cast<char*>(&zDim), s);

  // TODO support non-full BST trees? Right now, the number of timesteps
  // needs to be a power of two. Abort if it's not.
  if ( (numTimesteps & (numTimesteps-1)) != 0) {
    std::cout << "ERROR: Number of timesteps not  power of two" << std::endl;
    return false;
  }

  header_->SetDataDimensionality(dataDimensionality);
  header_->SetNumTimesteps(numTimesteps);

  if (xDim % xBrickDim_ !=0 ||yDim % yBrickDim_ !=0||zDim % zBrickDim_ !=0) {
    std::cout << "Error: Voxel and brick dimension mismatch!" << std::endl;
    instream_.close();
    delete header_;
    return false;
  }

  unsigned int xNumBricks = xDim / xBrickDim_;
  unsigned int yNumBricks = yDim / yBrickDim_;
  unsigned int zNumBricks = zDim / zBrickDim_;
  header_->SetNumBricks(xNumBricks, yNumBricks, zNumBricks);

  std::cout << "Read from " << inFilename_ << " complete!" << std::endl;
  std::cout << "Data dimensionality: " << dataDimensionality << std::endl;
  std::cout << "Number of timesteps: " << numTimesteps << std::endl;
  std::cout << "Dimensions: " << xDim << " x " << yDim <<  
               " x " << zDim << std::endl;
  std::cout << "Brick dimensions: " << xBrickDim_ << " x " << yBrickDim_ <<
               " x " << zBrickDim_ << std::endl;
  std::cout << "Number of bricks: " << xNumBricks << " x " << yNumBricks << 
               " x " << zNumBricks << std::endl;
  std::cout << "Structure: " << structure_ << std::endl;
  std::cout << "Data size (bytes): " << header_->DataSize() << std::endl;
  std::cout << "Out file name: " << outFilename_ << std::endl;

  // Calculate some common things

  // Number of bricks in the base (leaf) level
  nrBricksBaseLevel_ = xNumBricks*yNumBricks*zNumBricks;
  // Number of octree levels
  nrLevels_ = log(xNumBricks)/log(2) + 1;
  // Number of bricks per octree (used for offsets)
  nrBricksPerOctree_ = (pow(8, nrLevels_) - 1) / 7;

  std::cout << "Number of bricks in base octree level: " 
    << nrBricksBaseLevel_ << std::endl;
  std::cout << "Number of levels in octree: " << nrLevels_ << std::endl;
  std::cout << "Number of bricks in octree: " 
    << nrBricksPerOctree_ << std::endl;

  // Save position of first data entry after header
  headerOffset_ = instream_.tellg();
  
  instream_.close();

  return true;
}

bool Forge::CreateOctree() {
  
  if (!header_) {
    std::cout << "Error: No header" << std::endl;
    return false;
  }

  // Init out file
  std::fstream out;
  out.open(tempFilename_.c_str(),
           std::ios_base::out | std::ios_base::binary | std::ios_base::trunc);
  if (!out.is_open()) {
    std::cout << "Could not open " << tempFilename_ << " for write"<<std::endl;
    return false;
  }
  unsigned int outpos = 0;

  // Init in file
  std::fstream in;
  in.open(inFilename_.c_str(), std::ios_base::in | std::ios_base::binary);
  if (!in.is_open()) {
    std::cout << "Could not open " << inFilename_ <<" for reading"<<std::endl;
  }

  // Loop over all timesteps
  for (unsigned int i=0; i<header_->NumTimesteps(); ++i) {
    
    std::cout << "Constructing octree for timestep " << i << std::endl;
    // Construct bricks for this timestep (regular order)
    
    // Read whole timestep into memory

    // Brick dimensions
    unsigned int xBrickDim = header_->XBrickDim();
    unsigned int yBrickDim = header_->YBrickDim();
    unsigned int zBrickDim = header_->ZBrickDim();
    unsigned int xNumBricks = header_->XNumBricks();
    unsigned int yNumBricks = header_->YNumBricks();
    unsigned int zNumBricks = header_->ZNumBricks();

    // Voxel dimensions
    unsigned int xDim = xBrickDim*xNumBricks;
    unsigned int yDim = yBrickDim*yNumBricks;
    unsigned int zDim = zBrickDim*zNumBricks;

    std::vector<real> timestepData(xDim*yDim*zDim, static_cast<real>(0));

    // Point to the right position in the file stream
    unsigned int timestepSize = xDim*yDim*zDim*sizeof(real);
    std::ios::pos_type timestepOffset = 
      static_cast<std::ios::pos_type>(i*timestepSize) + headerOffset_;
    in.seekg(timestepOffset);
    // Read data
    in.read(reinterpret_cast<char*>(&timestepData[0]), timestepSize);

    // Create a container for the base level bricks.
    std::vector<Brick<real>* > baseLevelBricks(nrBricksBaseLevel_, NULL);

    // Loop over the volume's subvolumes and create one brick for each
    for (unsigned int zBrick=0; zBrick<zNumBricks; ++zBrick) {
      for (unsigned int yBrick=0; yBrick<yNumBricks; ++yBrick) {
        for (unsigned int xBrick=0; xBrick<xNumBricks; ++xBrick) {
        
          Brick<real> *brick = Brick<real>::New(xBrickDim, 
                                                yBrickDim, 
                                                zBrickDim,
                                                static_cast<real>(0));  
            
          // Loop over the subvolume's voxels
          unsigned int xMin = xBrick * xBrickDim_;
          unsigned int xMax = (xBrick + 1) * xBrickDim_ - 1;
          unsigned int yMin = yBrick * yBrickDim_;
          unsigned int yMax = (yBrick + 1) * yBrickDim_ - 1;
          unsigned int zMin = zBrick * zBrickDim_;
          unsigned int zMax = (zBrick + 1) * zBrickDim_ - 1;
          unsigned int zLoc= 0;
          for (unsigned int zSub=zMin; zSub<=zMax; ++zSub) {
            unsigned int yLoc = 0;
            for (unsigned int ySub=yMin; ySub<=yMax; ++ySub) {  
              unsigned int xLoc = 0;
              for (unsigned int xSub=xMin; xSub<=xMax; ++xSub) {
                // Look up global index in full volume
                unsigned int globalIndex = 
                  xSub + ySub*xDim + zSub*xDim*yDim;
                // Set data at local subvolume index

                brick->SetData(xLoc, yLoc, zLoc, timestepData[globalIndex]); 
                xLoc++;
              }
              yLoc++;
            }
            zLoc++;
          }


          // Save to base level
          unsigned int brickIndex = 
            xBrick + yBrick*xNumBricks + zBrick*xNumBricks*yNumBricks;
            /*
          delete brick;
          brick = Brick<real>::New(xBrickDim,
                                   yBrickDim,
                                   zBrickDim,
                                   static_cast<real>((float)brickIndex/64.0));
          */
          baseLevelBricks[brickIndex] = brick;
        }
      }
    }

    // Make a container for all the octree bricks
    std::vector<Brick<real>* > octreeBricks(nrBricksPerOctree_);
    // Use Z-order coordinates to rearrange the base level bricks

    // so that the eight children for each parent node lies
    // next to each other
    for (uint16_t z=0; z<(uint16_t)zNumBricks; ++z) { 
      for (uint16_t y=0; y<(uint16_t)yNumBricks; ++y) {
        for (uint16_t x=0; x<(uint16_t)xNumBricks; ++x) {
          unsigned int zOrderIdx = static_cast<unsigned int>(ZOrder(x, y, z));
          unsigned int idx = x + y*xNumBricks + z*xNumBricks*yNumBricks;
          octreeBricks[zOrderIdx] = baseLevelBricks[idx];

        }
      }
    }

    // Construct higher levels of octree

    // Position for next brick, starting at position beyond base level
    unsigned int brickPos = nrBricksBaseLevel_;
    // Position for first child to average
    unsigned int childPos = 0;

    while (brickPos < nrBricksPerOctree_) {

      // Filter the eight children and then combine them to build
      // the higher level node
      std::vector<Brick<real>* > filteredChildren(8, NULL);
      unsigned int i=0;
      for (unsigned int child=childPos; child<childPos+8; ++child) {
        Brick<real> *filteredChild = 
          Brick<real>::Filter(octreeBricks[child]);
        filteredChildren[i++] = filteredChild;
      }
      Brick<real> *newBrick = Brick<real>::Combine(filteredChildren);
      
      for (auto it=filteredChildren.begin(); it!=filteredChildren.end(); ++it){
        delete *it;
        *it = NULL;
      }

      // Set next child pos
      childPos += 8;

      // Save new brick
      octreeBricks[brickPos++] = newBrick;
    }

    // Write octree to file
    int n = 0;
    for (auto it=octreeBricks.begin(); it!=octreeBricks.end(); ++it) {
      out.write(reinterpret_cast<char*>(&(*it)->data_[0]), (*it)->Size());
      // Free memory when we're done
      delete *it;
    }
    
  }  
 
  in.close();
  out.close();

  return true;
}

// Adapted from  http://graphics.stanford.edu/~seander/bithacks.htm
uint32_t Forge::ZOrder(uint16_t xPos, uint16_t yPos, uint16_t zPos) {
  uint32_t x = static_cast<uint32_t>(xPos);
  uint32_t y = static_cast<uint32_t>(yPos);
  uint32_t z = static_cast<uint32_t>(zPos);
  x = (x | (x << 16)) & 0x030000FF;
  x = (x | (x <<  8)) & 0x0300F00F;
  x = (x | (x <<  4)) & 0x030C30C3;
  x = (x | (x <<  2)) & 0x09249249;
  y = (y | (y << 16)) & 0x030000FF;
  y = (y | (y <<  8)) & 0x0300F00F;
  y = (y | (y <<  4)) & 0x030C30C3;
  y = (y | (y <<  2)) & 0x09249249;
  z = (z | (z << 16)) & 0x030000FF;
  z = (z | (z <<  8)) & 0x0300F00F;
  z = (z | (z <<  4)) & 0x030C30C3;
  z = (z | (z <<  2)) & 0x09249249;
  const uint32_t result = x | (y << 1) | (z << 2);
  return result;
}


bool Forge::DeleteTempFile() {
  if (boost::filesystem::exists(tempFilename_)) {
    boost::filesystem::remove_all(tempFilename_);
  } else {
    std::cout <<"Warning: " << tempFilename_ << " does not exist" << std::endl;
  }
  return true;
}

bool Forge::ConstructTSPTree() {

  // Make sure the temporary file exists
  if (!boost::filesystem::exists(tempFilename_)) {
    std::cout << "Error: Temp file "<<tempFilename_ <<" missing"<< std::endl;
    return false;
  }
  std::fstream in;
  in.open(tempFilename_.c_str(), std::ios_base::in | std::ios_base::binary);
  if (!in.is_open()) {
    std::cout << "Error: Could not open " << tempFilename_ << std::endl;
    return false;
  }

  // Number of nodes in the octree skeleton
  unsigned int numOctreeNodes = nrBricksPerOctree_;
  std::cout << "Number of nodes per octree " << numOctreeNodes << std::endl;

  // Number of nodes in binary time tree
  unsigned int numBSTNodes = 2*header_->NumTimesteps() - 1;
  std::cout << "Num nodes in BST: " << numBSTNodes << std::endl;

  std::fstream out;
  out.open(tspFilename_.c_str(), 
           std::ios_base::out | std::ios_base::binary | std::ios_base::trunc);
  if (!out.is_open()) {
    std::cout << "Error: could not open " << tspFilename_ << std::endl;
    return false;
  }
  
  // Write header
  unsigned int structure = header_->Structure();
  unsigned int dataDimensionality = header_->DataDimensionality();
  unsigned int xBrickDim = header_->XBrickDim();
  unsigned int yBrickDim = header_->YBrickDim();
  unsigned int zBrickDim = header_->ZBrickDim();
  unsigned int xNumBricks = header_->XNumBricks();
  unsigned int yNumBricks = header_->YNumBricks();
  unsigned int zNumBricks = header_->ZNumBricks();
  unsigned int numTimesteps = header_->NumTimesteps();
  unsigned int paddingWidth = header_->PaddingWidth();
  unsigned int dataSize = header_->DataSize();
  out.seekp(std::ios_base::beg);
  std::cout << "Writing header" << std::endl;
  // Write header
  size_t s = sizeof(unsigned int);
  out.write(reinterpret_cast<char*>(&structure), s);
  out.write(reinterpret_cast<char*>(&dataDimensionality), s);
  out.write(reinterpret_cast<char*>(&xBrickDim), s);
  out.write(reinterpret_cast<char*>(&yBrickDim), s);
  out.write(reinterpret_cast<char*>(&zBrickDim), s);
  out.write(reinterpret_cast<char*>(&xNumBricks), s);
  out.write(reinterpret_cast<char*>(&yNumBricks), s);
  out.write(reinterpret_cast<char*>(&zNumBricks), s);
  out.write(reinterpret_cast<char*>(&numTimesteps), s);
  out.write(reinterpret_cast<char*>(&paddingWidth), s);
  out.write(reinterpret_cast<char*>(&dataSize), s);

  std::cout << "Position after writing header: " << out.tellp() << std::endl;

  // Loop over all positions in octree skeleton
  
  // Octree level (to both read from and write to)
  unsigned int level = 0;

  // Number of values in a brick
  unsigned int brickSize = xBrickDim * yBrickDim * zBrickDim; 
  std::cout << "Num values in brick " << brickSize << std::endl;

  // Position in octree to read from (starting at first timestep)
  // Since the octree is constructed from the base level up,
  // the root is at the last brick index
  unsigned int octreePos = (numOctreeNodes)*brickSize;

  // Loop over all octree levels (in backwards order)
  while (level < nrLevels_) {

    unsigned int bricksPerLevel = pow(8, level);
    unsigned int valuesPerLevel = brickSize * bricksPerLevel;
    octreePos -= valuesPerLevel;
    std::cout << "Level " << level << ", starting octree pos: " << octreePos << std::endl;
    
    // Loop over all octree nodes in this level
    for (unsigned int i=0; i<bricksPerLevel; ++i) {

      // Allocate bricks for one BST
      std::vector<Brick<real>* > BSTBricks(numBSTNodes);

      // Starting position in the BST (base level first)
      // Note that we want the root to be in the front
      unsigned int BSTBrickPos = numBSTNodes - header_->NumTimesteps();

      // Collect all corresponding bricks from the octree and build the leaves
      for (unsigned int ts=0; ts<header_->NumTimesteps(); ++ts) {
        Brick<real> *brick = Brick<real>::New(header_->XBrickDim(),
                                              header_->YBrickDim(),
                                              header_->ZBrickDim(),
                                              static_cast<real>(0));
        real *dataPtr = &(brick->data_[0]);
        size_t s = brickSize*sizeof(real);
        in.seekg(static_cast<std::ios::pos_type>((octreePos+ts*numOctreeNodes*brickSize)*sizeof(real)));
        in.read(reinterpret_cast<char*>(dataPtr), s);

        BSTBricks[BSTBrickPos++] = brick;
      }
      // Rewind position
      BSTBrickPos -= header_->NumTimesteps();


      // Average bricks to build higher levels in BST
      // This is really a reversed level, but it works well for loop purposes
      unsigned int level = 1;
      do {
        // Save position for bricks to average from
        unsigned int fromPos = BSTBrickPos;
        // Calculate starting position
        unsigned int bricksAtLevel = header_->NumTimesteps()/(pow(2, level));
        BSTBrickPos -= bricksAtLevel;
        // Average bricks 
        for (int i=0; i<bricksAtLevel; ++i) {
          //std::cout << fromPos << " & " << fromPos+1 << " -> " << BSTBrickPos << std::endl;
          BSTBricks[BSTBrickPos] = Brick<real>::Average(BSTBricks[fromPos],
                                                        BSTBricks[fromPos+1]);
          BSTBrickPos++;
          fromPos += 2;
        }
        level++;
        BSTBrickPos -= bricksAtLevel;
      } while (BSTBrickPos > 0);
      
      // Write BST to file
      unsigned int s = brickSize*sizeof(real);
      for (auto it=BSTBricks.begin(); it!=BSTBricks.end(); ++it) {
        out.write(reinterpret_cast<char*>(&(*it)->data_[0]), s);   
      }

      octreePos += brickSize;
 
    }
    // Rewind
    octreePos -= valuesPerLevel;

    level++;

  } // while level < nrLevels

  return true;
} 


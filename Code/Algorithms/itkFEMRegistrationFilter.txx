
/*=========================================================================

  Program:   Insight Segmentation & Registration Toolkit
  Module:    itkFEMRegistrationFilter.txx
  Language:  C++

  Date:      $Date$
  Version:   $Revision$

  Copyright (c) 2002 Insight Consortium. All rights reserved.
  See ITKCopyright.txt or http://www.itk.org/HTML/Copyright.htm for detail.

     This software is distributed WITHOUT ANY WARRANTY; without even 
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notices for more information.

=========================================================================*/

#ifndef _itkFEMRegistrationFilter_txx_
#define _itkFEMRegistrationFilter_txx_

// disable debug warnings in MS compiler
#ifdef _MSC_VER
#pragma warning(disable: 4786)
#endif
 
#include "itkFEMRegistrationFilter.h"
#include "itkFEMElements.h"
#include "itkFEMLoadBC.h"


#include "itkImageFileWriter.h"
#include "itkImageFileReader.h"
#include "itkCastImageFilter.h"
#include "itkDiscreteGaussianImageFilter.h"
#include "itkDerivativeImageFilter.h"
#include "itkVectorNeighborhoodOperatorImageFilter.h"
#include "itkNearestNeighborInterpolateImageFunction.h"
#include "itkBSplineInterpolateImageFunction.h"
#include "itkLinearInterpolateImageFunction.h"

#include "vnl/algo/vnl_determinant.h"

namespace itk {
namespace fem {


template<class TMovingImage,class TFixedImage>
FEMRegistrationFilter<TMovingImage,TFixedImage>::~FEMRegistrationFilter( )
{
}


template<class TMovingImage,class TFixedImage>
FEMRegistrationFilter<TMovingImage,TFixedImage>::FEMRegistrationFilter( )
{
  m_FileCount=0; 
 
  m_MinE=0;
  m_MinE=vnl_huge_val(m_MinE);  

  m_DescentDirection=positive;
  m_E.resize(1);
  m_Gamma.resize(1);
  m_Rho.resize(1);
  m_E[0]=1.; 
  m_Rho[0]=1.;
  m_CurrentLevel=0;
  m_Maxiters.resize(1);
  m_Maxiters[m_CurrentLevel]=1;  
  m_dT=1;
  m_Alpha=1.0;
  m_Temp=0.0;
  m_MeshPixelsPerElementAtEachResolution.resize(1);
  m_NumberOfIntegrationPoints.resize(1);
  m_NumberOfIntegrationPoints[m_CurrentLevel]=4;
  m_MetricWidth.resize(1);
  m_MetricWidth[m_CurrentLevel]=3;
  m_DoLineSearchOnImageEnergy=1;
  m_LineSearchMaximumIterations=100;
  m_UseMassMatrix=true;

  m_NumLevels=1;
  m_MaxLevel=1;
  m_MeshStep=2;
  m_MeshLevels=1;
  m_DoMultiRes=false;
  m_UseLandmarks=false;
  m_MinJacobian=1.0;

  m_FixedPyramid=NULL;
  m_MovingPyramid=NULL;
  m_TotalIterations=0;

  m_ReadMeshFile=false;

  for (unsigned int i=0; i < ImageDimension; i++)
    {
    m_ImageScaling[i]=1;
    m_FullImageSize[i]=0;
    m_ImageOrigin[i]=0;
    }
  m_FloatImage=NULL;
  m_Field=NULL;
  m_TotalField=NULL;
}


template<class TMovingImage,class TFixedImage>
void FEMRegistrationFilter<TMovingImage,TFixedImage>::RunRegistration()
{
  std::cout << "beginning registration\n";  

  // Solve the system in time 

  if (!m_DoMultiRes && m_Maxiters[m_CurrentLevel] > 0) 
    {
    SolverType mySolver;
    mySolver.SetDeltatT(m_dT);  
    mySolver.SetRho(m_Rho[m_CurrentLevel]);      
    mySolver.SetAlpha(m_Alpha); 
    
    CreateMesh((double)m_MeshPixelsPerElementAtEachResolution[m_CurrentLevel],mySolver,m_FullImageSize); 
    ApplyLoads(mySolver,m_FullImageSize);

    unsigned int ndofpernode=(m_Element)->GetNumberOfDegreesOfFreedomPerNode();
    unsigned int numnodesperelt=(m_Element)->GetNumberOfNodes()+1;
    unsigned int ndof=mySolver.GetNumberOfDegreesOfFreedom();
    unsigned int nzelts;
    if (!m_ReadMeshFile) nzelts=numnodesperelt*ndofpernode*ndof; 
    else nzelts=((2*numnodesperelt*ndofpernode*ndof > 25*ndof) ? 2*numnodesperelt*ndofpernode*ndof : 25*ndof);
    
    LinearSystemWrapperItpack itpackWrapper; 
    itpackWrapper.SetMaximumNonZeroValuesInMatrix(nzelts);
    itpackWrapper.SetMaximumNumberIterations(2*mySolver.GetNumberOfDegreesOfFreedom()); 
    itpackWrapper.SetTolerance(1.e-1);
//    itpackWrapper.JacobianSemiIterative(); 
    itpackWrapper.JacobianConjugateGradient(); 
    mySolver.SetLinearSystemWrapper(&itpackWrapper); 

    if( m_UseMassMatrix ) mySolver.AssembleKandM(); 
    else 
      {
      mySolver.InitializeForSolution();
      mySolver.AssembleK();

    }
    IterativeSolve(mySolver);


//    InterpolateVectorField(&mySolver);
  }
  else if (m_Maxiters[m_CurrentLevel] > 0)
    {
    MultiResSolve();
    }
    
    this->ComputeJacobian(-1.,m_Field);
    WarpImage(m_MovingImage); 
    if (m_TotalField) m_Field=m_TotalField;
}



template<class TMovingImage,class TFixedImage>
void FEMRegistrationFilter<TMovingImage,TFixedImage>::SetMovingImage(ImageType* R)
{
  m_MovingImage=R;
}



template<class TMovingImage,class TFixedImage>
void FEMRegistrationFilter<TMovingImage,TFixedImage>::SetFixedImage(FixedImageType* T)
{
  m_FixedImage=T;
  m_FullImageSize = m_FixedImage->GetLargestPossibleRegion().GetSize();
  
  VectorType disp;
  for (unsigned int i=0; i < ImageDimension; i++) 
    {
    disp[i]=0.0;
    m_ImageOrigin[i]=0;

  } 

  m_Wregion.SetSize( m_FullImageSize );
  m_WarpedImage = ImageType::New();
  m_WarpedImage->SetLargestPossibleRegion( m_Wregion );
  m_WarpedImage->SetBufferedRegion( m_Wregion );
  m_WarpedImage->Allocate();


  
  m_CurrentLevelImageSize=m_FullImageSize;

}


template<class TMovingImage,class TFixedImage>
void FEMRegistrationFilter<TMovingImage,TFixedImage>::ChooseMetric(float which)
{
  // Choose the similarity metric


// for using the imagetoimagemetricloads 
#ifdef  USEIMAGEMETRIC

  typedef itk::MeanSquaresImageToImageMetric<FixedImageType,MovingImageType> MetricType0;
  typedef itk::NormalizedCorrelationImageToImageMetric<FixedImageType,MovingImageType> MetricType1;
  typedef itk::PatternIntensityImageToImageMetric<FixedImageType,MovingImageType> MetricType2;
  typedef itk::MutualInformationImageToImageMetric<FixedImageType,MovingImageType> MetricType3;
  typedef itk::MattesMutualInformationImageToImageMetric<FixedImageType,MovingImageType> MetricType4;
//  typedef itk::DemonsImageToImageMetric<FixedImageType,MovingImageType> MetricType5;

  typedef itk::MeanSquaresImageToImageMetric<FixedImageType,MovingImageType> MetricType5;

  float m_Temp=1.0;

  MetricType3::Pointer m=MetricType3::New();
  MetricType4::Pointer ma=MetricType4::New();

  unsigned int whichmetric=(unsigned int) which;

  m_WhichMetric=which;
  unsigned int nsp=1;
  for (int i=0; i<ImageDimension; i++) nsp*=m_MetricWidth[m_CurrentLevel];

  if (whichmetric == 3 ) {
  m->SetNumberOfSpatialSamples(nsp/2); 
  m->SetFixedImageStandardDeviation(0.4);
  m->SetMovingImageStandardDeviation(0.4);
  }
  else if (whichmetric == 4 ) {
  ma->SetNumberOfHistogramBins( 10 );
  ma->SetNumberOfSpatialSamples( nsp/2 );
  } 

  switch (whichmetric){
    case 0:
      m_Metric=MetricType0::New();
      m_Metric->SetScaleGradient(m_Temp); // this is the default(?) 
      //p=MetricType0::New();
      //m_Function->SetInverseMetric(p);
      std::cout << " Mean Square " << std::endl;
      break;
    case 1:
      m_Metric=MetricType1::New();
      m_Metric->SetScaleGradient(m_Temp); 
      std::cout << " Normalized Correlation " << std::endl;
      break;
    case 2:
      m_Metric=MetricType2::New();
      m_Metric->SetScaleGradient(m_Temp); 
      std::cout << " Pattern Intensity " << std::endl;
      break;
    case 3:
       m_Metric=m;
      m_Metric->SetScaleGradient(m_Temp); 
    std::cout << " Mutual Information " << std::endl;
  break;
    case 4:
    m_Metric=ma;
      m_Metric->SetScaleGradient(m_Temp); 
    std::cout << " Mattes Mutual Information " << std::endl;
  break;
    case 5:
    m_Metric=MetricType5::New();
    m_Metric->SetScaleGradient(m_Temp); 
    std::cout << " Demons " << std::endl;
  break;
    default:
      m_Metric=MetricType0::New();
      m_Metric->SetScaleGradient(m_Temp); 

      std::cout << " Mean Square  " <<std::endl;
  }
#else 


  typedef itk::MeanSquareRegistrationFunction<FixedImageType,MovingImageType,FieldType> MetricType0;
  typedef itk::NCCRegistrationFunction<FixedImageType,MovingImageType,FieldType> MetricType1;
  typedef itk::NCCRegistrationFunction<FixedImageType,MovingImageType,FieldType> MetricType2;
  typedef itk::MIRegistrationFunction<FixedImageType,MovingImageType,FieldType> MetricType3;
  typedef itk::MIRegistrationFunction<FixedImageType,MovingImageType,FieldType> MetricType4;
  typedef itk::DemonsRegistrationFunction<FixedImageType,MovingImageType,FieldType> MetricType5;

  typename MetricType3::Pointer m=MetricType3::New();
  typename MetricType4::Pointer ma=MetricType4::New();

  unsigned int whichmetric=(unsigned int) which;
  m_WhichMetric=(unsigned int)which;

  switch (whichmetric){
    case 0:
      m_Metric=MetricType0::New();
    std::cout << " Mean Square " << std::endl;
    break;
    case 1:
      m_Metric=MetricType1::New();
    std::cout << " Normalized Correlation " << std::endl;
    break;
    case 2:
      m_Metric=MetricType2::New();
    std::cout << " NCC  " << std::endl;
    break;
    case 3:
       m_Metric=m;
    std::cout << " Mutual Information " << std::endl;
    break;
    case 4:
      m_Metric=ma;
    std::cout << " Mattes Mutual Information " << std::endl;
    break;
    case 5:
      m_Metric=MetricType5::New();
    std::cout << " Demons " << std::endl;
    break;
    default:
      m_Metric=MetricType0::New();
      std::cout << " Mean Square  " <<std::endl;
    }


    m_Metric->SetGradientStep( m_Gamma[m_CurrentLevel] );
    if ( m_Temp == 1.0 ) m_Metric->SetNormalizeGradient(true);
    else m_Metric->SetNormalizeGradient(false);

    std::cout << " normalizing? " << m_Metric->GetNormalizeGradient() << std::endl;
#endif
}
 



template<class TMovingImage,class TFixedImage>
bool FEMRegistrationFilter<TMovingImage,TFixedImage>::ReadConfigFile(const char* fname)
  // Reads the parameters necessary to configure the example & returns
  // false if no configuration file is found
{
  std::ifstream f;
  char buffer[80] = {'\0'};
  Float fbuf = 0.0;
  unsigned int ibuf = 0;
  unsigned int jj;

  std::cout << "Reading config file..." << fname << std::endl;
  f.open(fname);
  if (f)
    {
  
    this->DoMultiRes(true);

    FEMLightObject::SkipWhiteSpace(f);
    f >> ibuf;
    this->m_NumLevels = (unsigned int) ibuf;

    FEMLightObject::SkipWhiteSpace(f);
    f >> ibuf;
    this->m_MaxLevel = ibuf;
     
    // get the initial scales for the pyramid
    FEMLightObject::SkipWhiteSpace(f);
    for (jj=0; jj<ImageDimension; jj++)
      {
      f >> ibuf;
      m_ImageScaling[jj] = ibuf;
      }
    
    this->m_MeshPixelsPerElementAtEachResolution.resize(m_NumLevels);
    FEMLightObject::SkipWhiteSpace(f);
    for (jj=0; jj<this->m_NumLevels; jj++)
      {
      f >> ibuf;
      this->m_MeshPixelsPerElementAtEachResolution(jj) = ibuf;
      }
    
    FEMLightObject::SkipWhiteSpace(f);
    this->m_E.resize(m_NumLevels);
    for (jj=0; jj<this->m_NumLevels; jj++)
      {
      f >> fbuf;
      this->SetElasticity(fbuf,jj);
      }

    FEMLightObject::SkipWhiteSpace(f);
    this->m_Rho.resize(m_NumLevels);
    for (jj=0; jj<this->m_NumLevels; jj++)
      {
      f >> fbuf;
      this->SetRho(fbuf,jj);
      }

    FEMLightObject::SkipWhiteSpace(f);
    this->m_Gamma.resize(m_NumLevels);
    for (jj=0; jj<this->m_NumLevels; jj++)
      {
      f >> fbuf;
      this->SetGamma(fbuf,jj);
      }

    FEMLightObject::SkipWhiteSpace(f);
    this->m_NumberOfIntegrationPoints.resize(m_NumLevels);
    for(jj=0; jj< m_NumLevels; jj++)
      { 
      f >> ibuf;
      this->SetNumberOfIntegrationPoints(ibuf,jj);
      }

    FEMLightObject::SkipWhiteSpace(f);
    this->m_MetricWidth.resize(m_NumLevels);
    for(jj=0; jj< m_NumLevels; jj++)
      { 
      f >> ibuf;
      this->SetWidthOfMetricRegion(ibuf,jj);
      }
    
    FEMLightObject::SkipWhiteSpace(f);
    this->m_Maxiters.resize(m_NumLevels);
    for (unsigned int jj=0; jj<this->m_NumLevels; jj++)
      {
      f >> ibuf;
      this->SetMaximumIterations(ibuf,jj);
      }

    FEMLightObject::SkipWhiteSpace(f);
    float fbuf2=1.0;
    f >> fbuf;  
    f >> fbuf2;
    m_Temp=fbuf2;
    this->ChooseMetric(fbuf);

    FEMLightObject::SkipWhiteSpace(f);
    f >> fbuf;
    this->m_Alpha=fbuf;
    
    FEMLightObject::SkipWhiteSpace(f);
    f >> ibuf;
    if (ibuf == 0)
      {
      this->SetDescentDirectionMinimize();
      }
    else
      {
      this->SetDescentDirectionMaximize();
      }

    FEMLightObject::SkipWhiteSpace(f);
    f >> ibuf;
    this->DoLineSearch(ibuf); 
    
    FEMLightObject::SkipWhiteSpace(f);
    f >> fbuf;
    this->SetTimeStep(fbuf);

    FEMLightObject::SkipWhiteSpace(f);
    f >> fbuf;
    this->SetEnergyReductionFactor(fbuf);

    FEMLightObject::SkipWhiteSpace(f);
    f >> ibuf;
    m_EmployRegridding = (bool) ibuf;

    FEMLightObject::SkipWhiteSpace(f);
    f >> ibuf;
    this->m_FullImageSize[0] = ibuf;

    FEMLightObject::SkipWhiteSpace(f);
    f >> ibuf;
    this->m_FullImageSize[1] = ibuf;

    FEMLightObject::SkipWhiteSpace(f);
    f >> ibuf;
    unsigned int dim=2;
    if (ibuf > 0) dim=3;
    if (dim == 3) this->m_FullImageSize[2] = ibuf;

    FEMLightObject::SkipWhiteSpace(f);
    f >> buffer;
    this->SetMovingFile(buffer);

    FEMLightObject::SkipWhiteSpace(f);
    f >> buffer;
    this->SetFixedFile(buffer);

    FEMLightObject::SkipWhiteSpace(f);
    f >> ibuf;
    FEMLightObject::SkipWhiteSpace(f);
    f >> buffer;

    if (ibuf == 1)
      {
      this->UseLandmarks(true);
      this->SetLandmarkFile(buffer);
      }
    else
      {
      this->UseLandmarks(false);
      }

    FEMLightObject::SkipWhiteSpace(f);
    f >> buffer;
    this->SetResultsFile(buffer);

    FEMLightObject::SkipWhiteSpace(f);
    f >> ibuf;
    FEMLightObject::SkipWhiteSpace(f);
    f >> buffer;

    if (ibuf == 1)
      {
      this->SetWriteDisplacements(true);
      this->SetDisplacementsFile(buffer);
      }
    else 
      {
      this->SetWriteDisplacements(false);
      }

    FEMLightObject::SkipWhiteSpace(f);
    f >> ibuf;
    FEMLightObject::SkipWhiteSpace(f);
    f >> buffer;

    if (ibuf == 1)
      {
      this->m_ReadMeshFile=true;
      this->m_MeshFileName=buffer;
      }
    else
      { 
      this->m_ReadMeshFile=false;
      }

    f.close();
    std::cout << "Example configured. E " << m_E << " rho " << m_Rho << std::endl;
    return true;
    }
  else
    { 
    std::cout << "No configuration file specified...quitting.\n"; 
    return false;
    }  
}


template<class TMovingImage,class TFixedImage>
int FEMRegistrationFilter<TMovingImage,TFixedImage>::WriteDisplacementField(unsigned int index)
  // Outputs the displacement field for the index provided (0=x,1=y,2=z)
{
  // Initialize the Moving to the displacement field
  typename IndexSelectCasterType::Pointer fieldCaster = IndexSelectCasterType::New();
  fieldCaster->SetInput( m_Field );
  fieldCaster->SetIndex( index );
  
  // Define the output of the Moving
  typename FloatImageType::Pointer fieldImage = FloatImageType::New();
  fieldCaster->Update();
  fieldImage = fieldCaster->GetOutput();

  // Set up the output filename
  std::string outfile=m_DisplacementsFileName+static_cast<char>('x'+index)+std::string("vec.hdr");
  std::cout << "Writing displacements to " << outfile;

  // Write the single-index field to a file
  //   itk::ImageRegionIteratorWithIndex<FloatImageType> it( fieldImage, fieldImage->GetLargestPossibleRegion() );
  //   for (; !it.IsAtEnd(); ++it) { std::cout << it.Get() << "\t"; }
  typedef typename FloatImageType::PixelType FType;
  typedef ImageFileWriter<FloatImageType> WriterType;
  typename WriterType::Pointer writer = WriterType::New();
  writer->SetInput(fieldImage);
  writer->SetFileName(outfile.c_str());
  writer->Write();

  std::cout << "...done" << std::endl;
  return 0;
}



template<class TMovingImage,class TFixedImage>
void FEMRegistrationFilter<TMovingImage,TFixedImage>::WarpImage( const ImageType * ImageToWarp)
{
  // -------------------------------------------------------
  std::cout << "Warping image" << std::endl;

  {
  typedef itk::WarpImageFilter<ImageType,ImageType,FieldType> WarperType;
  typename WarperType::Pointer warper = WarperType::New();


    typedef typename WarperType::CoordRepType CoordRepType;
    typedef itk::NearestNeighborInterpolateImageFunction<ImageType,CoordRepType>
      InterpolatorType0;
    typedef itk::LinearInterpolateImageFunction<ImageType,CoordRepType>
      InterpolatorType1;
    typedef itk::BSplineInterpolateImageFunction<ImageType,CoordRepType>
      InterpolatorType2;
    typename InterpolatorType1::Pointer interpolator = InterpolatorType1::New();
  
    warper = WarperType::New();
    warper->SetInput( ImageToWarp );
    warper->SetDeformationField( m_Field );
    warper->SetInterpolator( interpolator );
    warper->SetOutputSpacing( m_FixedImage->GetSpacing() );
    warper->SetOutputOrigin( m_FixedImage->GetOrigin() );
    typename ImageType::PixelType padValue = 0;
    warper->SetEdgePaddingValue( padValue );
    warper->Update();

    m_WarpedImage=warper->GetOutput();  
    }

}



template<class TMovingImage,class TFixedImage>
void FEMRegistrationFilter<TMovingImage,TFixedImage>::CreateMesh(double PixelsPerElement, 
    Solver& mySolver, ImageSizeType imagesize)
{

  vnl_vector<double> MeshOriginV; MeshOriginV.resize(ImageDimension); 
  vnl_vector<double> MeshSizeV;   MeshSizeV.resize(ImageDimension); 
  vnl_vector<double> ImageSizeV;   ImageSizeV.resize(ImageDimension); 
  vnl_vector<double> ElementsPerDim;  ElementsPerDim.resize(ImageDimension); 
  for (unsigned int i=0; i<ImageDimension; i++)

  { 
    MeshSizeV[i]=(double)imagesize[i]; // FIX ME  make more general

    MeshOriginV[i]=(double)m_ImageOrigin[i];// FIX ME make more general
    ImageSizeV[i]=(double) imagesize[i]+1;//to make sure all elts are inside the interpolation mesh
    ElementsPerDim[i]=MeshSizeV[i]/PixelsPerElement;

  }

  std::cout << " ElementsPerDim " << ElementsPerDim << std::endl;

  if (m_ReadMeshFile)
    {
    std::ifstream meshstream; 
    meshstream.open(m_MeshFileName.c_str());
    if (!meshstream)
      {
      std::cout<<"File "<<m_MeshFileName<<" not found!\n";
      return;
      }

    mySolver.Read(meshstream); 
    mySolver.GenerateGFN();
    itk::fem::MaterialLinearElasticity::Pointer m=dynamic_cast<MaterialLinearElasticity*>(mySolver.mat.Find(0));
   
    if (m)
      { 
      m->E=this->GetElasticity(m_CurrentLevel);  // Young modulus -- used in the membrane ///
      }   
    // now scale the mesh to the current scale
    Element::VectorType coord;  
    Node::ArrayType* nodes = &(mySolver.node);
    Node::ArrayType::iterator node=nodes->begin();
    m_Element=( *((*node)->m_elements.begin()));
    for(  node=nodes->begin(); node!=nodes->end(); node++) 
      {
      coord=(*node)->GetCoordinates();    
      for (unsigned int ii=0; ii < ImageDimension; ii++)
        { 
        coord[ii] = coord[ii]/(float)m_ImageScaling[ii];
        }
      (*node)->SetCoordinates(coord);  
      }
    }
  else if (ImageDimension == 2 && dynamic_cast<Element2DC0LinearQuadrilateral*>(m_Element) != NULL)
    {
    m_Material->E=this->GetElasticity(m_CurrentLevel);
    Generate2DRectilinearMesh(m_Element,mySolver,MeshOriginV,MeshSizeV,ElementsPerDim); 
    mySolver.GenerateGFN();

    std::cout << " init interpolation grid : im sz " << ImageSizeV << " MeshSize " << MeshSizeV << std::endl;
    mySolver.InitializeInterpolationGrid(ImageSizeV,MeshOriginV,MeshSizeV);
    std::cout << " done initializing interpolation grid " << std::endl;
    }
  else if ( ImageDimension == 3 && dynamic_cast<Element3DC0LinearHexahedron*>(m_Element) != NULL) 
    {
    m_Material->E=this->GetElasticity(m_CurrentLevel);
  
    std::cout << " generating regular mesh " << std::endl;
    Generate3DRectilinearMesh(m_Element,mySolver,MeshOriginV,MeshSizeV,ElementsPerDim); 
    mySolver.GenerateGFN();  
    std::cout << " generating regular mesh done " << std::endl;
// the global to local transf is too slow so don't do it.  
    std::cout << " DO NOT init interpolation grid : im sz " << ImageSizeV << " MeshSize " << MeshSizeV << std::endl;
    //mySolver.InitializeInterpolationGrid(ImageSizeV,MeshOriginV,MeshSizeV);
    //std::cout << " done initializing interpolation grid " << std::endl;
    }
  else 
    {  
    throw FEMException(__FILE__, __LINE__, "CreateMesh - wrong image or element type ");
    }
  
}


template<class TMovingImage,class TFixedImage>
void FEMRegistrationFilter<TMovingImage,TFixedImage>
::ApplyImageLoads(SolverType& mySolver, TMovingImage*  movingimg, TFixedImage* fixedimg )
{
      m_Load=FEMRegistrationFilter<TMovingImage,TFixedImage>::ImageMetricLoadType::New();
      m_Load->SetMovingImage(movingimg); 
      m_Load->SetFixedImage(fixedimg);  
      if (!m_Field) this->InitializeField();
   #ifndef USEIMAGEMETRIC
      m_Load->SetDeformationField(this->GetDeformationField());
   #endif
      m_Load->SetMetric(m_Metric);
      m_Load->InitializeMetric();
      m_Load->SetTemp(m_Temp);
      m_Load->SetGamma(m_Gamma[m_CurrentLevel]);
      ImageSizeType r;
      for (unsigned int dd=0; dd<ImageDimension; dd++) r[dd]=m_MetricWidth[m_CurrentLevel];
      m_Load->SetMetricRadius(r);
      m_Load->SetNumberOfIntegrationPoints(m_NumberOfIntegrationPoints[m_CurrentLevel]);
      m_Load->GN=mySolver.load.size()+1; //NOTE SETTING GN FOR FIND LATER
      m_Load->SetSign((Float)m_DescentDirection);
      mySolver.load.push_back( FEMP<Load>(&*m_Load) );    
      m_Load=dynamic_cast<FEMRegistrationFilter<TMovingImage,TFixedImage>::ImageMetricLoadType*> 
          (&*mySolver.load.Find(mySolver.load.size()));  
}   


template<class TMovingImage,class TFixedImage>
void FEMRegistrationFilter<TMovingImage,TFixedImage>::ApplyLoads(SolverType& mySolver,ImageSizeType ImgSz, double* scaling)
{
  //
  // Apply the boundary conditions.  We pin the image corners.
  // First compute which elements these will be.
  ///
  std::cout << " applying loads " << std::endl;

  vnl_vector<Float> pd; pd.resize(ImageDimension);
  vnl_vector<Float> pu; pu.resize(ImageDimension);

  if (m_UseLandmarks)
  {
    
    LoadArray::iterator loaditerator;
    LoadLandmark::Pointer l3;

    if ( this->m_LandmarkArray.empty() ) 
    {
      // Landmark loads
      std::ifstream f;
      std::cout << m_LandmarkFileName << std::endl;
      f.open(m_LandmarkFileName.c_str());
      if (f)
      {

        std::cout << "Try loading landmarks..." << std::endl;
        std::cout << "Try loading landmarks..." << std::endl;
        try
        { 
          mySolver.load.clear(); // NOTE: CLEARING ALL LOADS - LMS MUST BE APPLIED FIRST
          mySolver.Read(f);
        }
        catch (itk::ExceptionObject &err)
        { 
          std::cerr << "Exception: cannot read load landmark FEMRegistrationFilter.txx " << err;
        }
        f.close();
        
        m_LandmarkArray.resize(mySolver.load.size());
        unsigned int ct=0;
        for(loaditerator=mySolver.load.begin(); loaditerator!=mySolver.load.end(); loaditerator++) 
        {
          if (l3=dynamic_cast<LoadLandmark*>( &(*(*loaditerator)) ) ) 
          {
            LoadLandmark::Pointer l4=dynamic_cast<LoadLandmark*>(l3->Clone());
            m_LandmarkArray[ct]=l4;
            ct++;
          }
        }

        mySolver.load.clear(); // NOTE: CLEARING ALL LOADS - LMS MUST BE APPLIED FIRST
      }
      else
      {
      std::cout << "no landmark file specified." << std::endl;
      }
  
    }
    
    
// now scale the landmarks

        std::cout << " num of LM loads " << m_LandmarkArray.size() << std::endl;
    /*
     * Step over all the loads again to scale them by the global landmark weight.
     */
     if ( !m_LandmarkArray.empty())
     {
     for(int lmind=0; lmind<m_LandmarkArray.size(); lmind++) 
     {
     
         m_LandmarkArray[lmind]->el[0]=NULL;
         bool isFound=false;
  std::cout << " prescale Pt " <<  m_LandmarkArray[lmind]->GetTarget() << std::endl;
         if (scaling) 
          {
           m_LandmarkArray[lmind]->ScalePointAndForce(scaling,m_EnergyReductionFactor);
  std::cout << " postscale Pt " <<  m_LandmarkArray[lmind]->GetTarget() << " scale " << scaling[0] << std::endl;
          }           
            
           pu=m_LandmarkArray[lmind]->GetSource();
           pd=m_LandmarkArray[lmind]->GetPoint();

         for (Element::ArrayType::const_iterator n = mySolver.el.begin(); 
                                    n!=mySolver.el.end() && !isFound; n++) 
         {
           if ( (*n)->GetLocalFromGlobalCoordinates(pu, pd ) )
           { 
             isFound=true;
             m_LandmarkArray[lmind]->SetPoint(pd);
//             std::cout << " load local pt " << m_LandmarkArray[lmind]->GetPoint() << std::endl;
             m_LandmarkArray[lmind]->el[0]=( ( &**n ) );
           }
         }

         m_LandmarkArray[lmind]->GN=lmind;            
         LoadLandmark::Pointer l5=(dynamic_cast<LoadLandmark::Pointer>(m_LandmarkArray[lmind]->Clone()));
         mySolver.load.push_back(FEMP<Load>(l5)); 
       }
     }
      std::cout << " landmarks done" << std::endl;
   }
     
    
   
 

// now apply the BC loads
  LoadBC::Pointer l1;

  //Pin  one corner of image  
  unsigned int CornerCounter,ii,EdgeCounter=0;
  Node::ArrayType* nodes = &(mySolver.node);
  Element::VectorType coord;
  Node::ArrayType::iterator node=nodes->begin();
  bool EdgeFound;
  unsigned int nodect=0;
  while(   node!=nodes->end() && EdgeCounter < ImageDimension ) 
  {
    coord=(*node)->GetCoordinates();
    CornerCounter=0;
    for (ii=0; ii < ImageDimension; ii++)
    { 
      if (coord[ii] == m_ImageOrigin[ii] || coord[ii] == ImgSz[ii]-1 ) CornerCounter++;
    }
    if (CornerCounter == ImageDimension) // the node is located at a true corner
    {
      unsigned int ndofpernode=(*((*node)->m_elements.begin()))->GetNumberOfDegreesOfFreedomPerNode();
      unsigned int numnodesperelt=(*((*node)->m_elements.begin()))->GetNumberOfNodes();
      unsigned int whichnode;
     
      unsigned int maxnode=numnodesperelt-1;
      
      typedef typename Node::SetOfElements NodeEltSetType;
      for( NodeEltSetType::iterator elt=(*node)->m_elements.begin(); 
           elt!=(*node)->m_elements.end(); elt++) 
      {
        for (whichnode=0; whichnode<=maxnode; whichnode++)
        {
          coord=(*elt)->GetNode(whichnode)->GetCoordinates();
          CornerCounter=0;
          
          for (ii=0; ii < ImageDimension; ii++)
            if (coord[ii] == m_ImageOrigin[ii] || coord[ii] == ImgSz[ii]-1 ) 
              CornerCounter++;
        
        if (CornerCounter == ImageDimension - 1) EdgeFound=true; else EdgeFound=false;
        if (EdgeFound)
        {
          for (unsigned int jj=0; jj<ndofpernode; jj++)
          {
            std::cout << " which node " << whichnode << std::endl; 
            std::cout << " edge coord " << coord << std::endl;
            
            l1=LoadBC::New();
            // now we get the element from the node -- we assume we need fix the dof only once
            // even if more than one element shares it.
            l1->m_element= (*elt);  // Fixed bug TS 1/17/03 ( *((*node)->m_elements.begin())); 
            //l1->m_element= ( *((*node)->m_elements[ect-1])); 
            unsigned int localdof=whichnode*ndofpernode+jj;
            l1->m_dof=localdof; 
            l1->m_value=vnl_vector<double>(1,0.0);
            mySolver.load.push_back( FEMP<Load>(&*l1) );
          }
          EdgeCounter++;
        }
        }
      }//end elt loop
    }
    node++;
    nodect++;
    std::cout << " node " << nodect << std::endl;
  }//

  return;
}


template<class TMovingImage,class TFixedImage>
void FEMRegistrationFilter<TMovingImage,TFixedImage>::IterativeSolve(SolverType& mySolver)
{
  m_MinE=10.e99;
  Float LastE, deltE=0, lastdeltE;

  unsigned int iters=0;
  bool Done=false;
  unsigned int DLS=m_DoLineSearchOnImageEnergy;
  
  if (!m_Load)
  {  
    std::cout << " m_Load not initialized " << std::endl;
    return;
  }

  while ( !Done && iters < m_Maxiters[m_CurrentLevel] )
  {

  
//  Reset the variational similarity term to zero.
   
   LastE=m_Load->GetCurrentEnergy();
   m_Load->SetCurrentEnergy(0.0);
   m_Load->InitializeMetric();
   
   if (!m_Field) std::cout << " Big Error -- Field is NULL "; 
   
  
 //   Assemble the master force vector (from the applied loads)
   

   // if (iters == 1) // for testing 
   if( m_UseMassMatrix ) mySolver.AssembleFforTimeStep(); else mySolver.AssembleF();

  
   m_Load->PrintCurrentEnergy();
   
    
   // Solve the system of equations for displacements (u=K^-1*F)
   

   mySolver.Solve();  
  

   //mySolver.PrintDisplacements(0);
  

   Float mint=1.0; //,ImageSimilarity=0.0;
   
   //#ifdef USEIMAGEMETRIC
   //LastE=EvaluateResidual(*mySolver,mint);
   //#endif

   lastdeltE=deltE;
 
   #ifndef USEIMAGEMETRIC
   if (m_DescentDirection == 1) 
      deltE=(LastE - m_Load->GetCurrentEnergy());
   else deltE=(m_Load->GetCurrentEnergy() - LastE );
   #else
   if (m_DescentDirection == 1) 
     deltE=(m_Load->GetCurrentEnergy() - LastE );
   else deltE=(LastE - m_Load->GetCurrentEnergy());
   #endif

   if (  DLS==2 && deltE < 0.0 ) 
   {
     std::cout << " line search ";
     float tol = 1.0;//((0.01  < LastE) ? 0.01 : LastE/10.);
     LastE=this->GoldenSection(mySolver,tol,m_LineSearchMaximumIterations);
     deltE=(m_MinE-LastE);
     std::cout << " line search done " << std::endl;
   } 

   iters++;

   if (deltE == 0.0)
   {
     std::cout << " no change in energy " << std::endl;
     Done=true;
   }
   if (DLS == 0) if (  iters >= m_Maxiters[m_CurrentLevel] )  Done=true; 
   if (DLS > 0) 
   if ( iters >= m_Maxiters[m_CurrentLevel] || (deltE < 0.0 && iters > 5 && lastdeltE < 0.0))
   Done=true;
   
   mySolver.AddToDisplacements(mint);
   m_MinE=LastE;

   InterpolateVectorField(mySolver);
   
   if ( iters % 25 == 0 && m_EmployRegridding)  
   {
     this->EnforceDiffeomorphism(0.2, mySolver); 
   }
    // uncomment to write out every deformation SLOW due to interpolating vector field everywhere.

    //if ( (iters % 1) == 0 || Done ) {
    //WarpImage(m_MovingImage);
    //WriteWarpedImage(m_ResultsFileName.c_str());
    //}//
 
    std::cout << " min E " << m_MinE <<  " delt E " << deltE <<  " iter " << iters << std::endl;
   
    m_TotalIterations++;
  } 
  
}


template<class TMovingImage,class TFixedImage>
void FEMRegistrationFilter<TMovingImage,TFixedImage>
::InitializeField()
{

std::cout << " allocating deformation field " << std::endl;

  m_Field = FieldType::New();

  m_FieldRegion.SetSize(m_CurrentLevelImageSize );
  m_Field->SetLargestPossibleRegion( m_FieldRegion );
  m_Field->SetBufferedRegion( m_FieldRegion );
  m_Field->SetLargestPossibleRegion( m_FieldRegion );
  m_Field->Allocate(); 
 
  VectorType disp;  
  for (unsigned int t=0; t<ImageDimension; t++) disp[t]=0.0;
  FieldIterator m_FieldIter( m_Field, m_FieldRegion );
  m_FieldIter.GoToBegin();
  for( ; !m_FieldIter.IsAtEnd(); ++m_FieldIter )
  {
    m_FieldIter.Set(disp);
  }
}



template<class TMovingImage,class TFixedImage>
void 
FEMRegistrationFilter<TMovingImage,TFixedImage>::InterpolateVectorField(SolverType& mySolver)
{

  typename FieldType::Pointer field=m_Field;

  if (!field)
  {
    this->InitializeField();
  }
  m_FieldSize=field->GetLargestPossibleRegion().GetSize();

  std::cout << " interpolating vector field of size " << m_FieldSize;
    
  
  Float rstep,sstep,tstep;

  vnl_vector<double> Pos;  // solution at the point
  vnl_vector<double> Sol;  // solution at the local point
  vnl_vector<double> Gpt;  // global position given by local point

 
  VectorType disp;  
  for (unsigned int t=0; t<ImageDimension; t++) disp[t]=0.0;
  FieldIterator m_FieldIter( field, field->GetLargestPossibleRegion() );

  m_FieldIter.GoToBegin();
  typename ImageType::IndexType rindex = m_FieldIter.GetIndex();

  Sol.resize(ImageDimension); 
  Gpt.resize(ImageDimension);

  if (ImageDimension == 2){
  Element::ConstPointer eltp;

  for( ; !m_FieldIter.IsAtEnd(); ++m_FieldIter )
  {
// get element pointer from the solver elt pointer image
    

    rindex = m_FieldIter.GetIndex();
    for(unsigned int d=0; d<ImageDimension; d++)
    {
      Gpt[d]=(double)rindex[d];
    }
    eltp=mySolver.GetElementAtPoint(Gpt);
    if (eltp)
    {
    eltp->GetLocalFromGlobalCoordinates(Gpt, Pos);
   
    unsigned int Nnodes= eltp->GetNumberOfNodes();
    typename Element::VectorType shapef(Nnodes);
    shapef = eltp->ShapeFunctions(Pos);
    Float solval;
    for(unsigned int f=0; f<ImageDimension; f++)
    {
      solval=0.0;
      for(unsigned int n=0; n<Nnodes; n++)
      {
        solval+=shapef[n] * mySolver.GetLS()->GetSolutionValue( 
          eltp->GetNode(n)->GetDegreeOfFreedom(f) , mySolver.TotalSolutionIndex);
      }
      Sol[f]=solval;
      disp[f] =(Float) 1.0*Sol[f];//*((Float)m_ImageScaling[f]); 
    }
    field->SetPixel(rindex, disp );
    }
  }
  }

  if (ImageDimension==3)
  {


  // FIXME SHOULD BE 2.0 over meshpixperelt
  rstep=1.25/((double)m_MeshPixelsPerElementAtEachResolution[m_CurrentLevel]);//
  sstep=1.25/((double)m_MeshPixelsPerElementAtEachResolution[m_CurrentLevel]);//
  tstep=1.25/((double)m_MeshPixelsPerElementAtEachResolution[m_CurrentLevel]);//
//  std::cout << " r s t steps " << rstep << " " << sstep << " "<< tstep << std::endl;

  Pos.resize(ImageDimension);
  for(  Element::ArrayType::iterator elt=mySolver.el.begin(); elt!=mySolver.el.end(); elt++) 
  {
    for (double r=-1.0; r <= 1.0; r=r+rstep ){
    for (double s=-1.0; s <= 1.0; s=s+sstep ){
    for (double t=-1.0; t <= 1.0; t=t+tstep ){
      Pos[0]=r; 
      Pos[1]=s;
      Pos[2]=t; 
  
        VectorType disp; 
        unsigned int Nnodes= (*elt)->GetNumberOfNodes();
        typename Element::VectorType shapef(Nnodes);

#define FASTHEX
#ifdef FASTHEX
//FIXME temporarily using hexahedron shape f for speed
            shapef[0] = (1 - r) * (1 - s) * (1 - t) * 0.125;
            shapef[1] = (1 + r) * (1 - s) * (1 - t) * 0.125;
            shapef[2] = (1 + r) * (1 + s) * (1 - t) * 0.125;
            shapef[3] = (1 - r) * (1 + s) * (1 - t) * 0.125;
            shapef[4] = (1 - r) * (1 - s) * (1 + t) * 0.125;
            shapef[5] = (1 + r) * (1 - s) * (1 + t) * 0.125;
            shapef[6] = (1 + r) * (1 + s) * (1 + t) * 0.125;
            shapef[7] = (1 - r) * (1 + s) * (1 + t) * 0.125;
#else
            shapef = (*elt)->ShapeFunctions(Pos);
#endif

        Float solval,posval;
        bool inimage=true;
  
        for(unsigned int f=0; f<ImageDimension; f++)
        {
          solval=0.0;
          posval=0.0;
          for(unsigned int n=0; n<Nnodes; n++)
          {
            posval+=shapef[n]*(((*elt)->GetNodeCoordinates(n))[f]);
            solval+=shapef[n] * mySolver.GetLS()->GetSolutionValue( 
              (*elt)->GetNode(n)->GetDegreeOfFreedom(f) , mySolver.TotalSolutionIndex);
          }
          Sol[f]=solval;
          Gpt[f]=posval;

          Float x=Gpt[f];
          long int temp;
          if (x !=0) temp=(long int) ((x)+0.5); else temp=0;// round 
          rindex[f]=temp;
          disp[f] =(Float) 1.0*Sol[f];
          if ( temp < 0 || temp > (long int) m_FieldSize[f]-1)  inimage=false;
        }

        if (inimage) field->SetPixel(rindex, disp );
  }
  }
  }//end of for loops

  } // end of elt array loop
  
  }/* */ // end if imagedimension==3
  
  // Insure that the values are exact at the nodes. They won't necessarily be unless we use this code.
 


}


 /*Node::ArrayType* nodes = &(mySolver.node);
  Element::VectorType coord;  
  VectorType SolutionAtNode;
  for(  Node::ArrayType::iterator node=node->begin(); node!=nodes->end(); node++) 
  {
    coord=(*node)->GetCoordinates();
    for (unsigned int ii=0; ii < ImageDimension; ii++)
    { 
      if (coord[ii] != 0) rindex[ii]=(long int) (coord[ii])-1; 
       else rindex[ii]=0;
      Float OldSol=mySolver.GetLinearSystemWrapper()->
        GetSolutionValue((*node)->GetDegreeOfFreedom(ii),mySolver.TotalSolutionIndex);
      SolutionAtNode[ii]=OldSol;    
    }
    m_Field->SetPixel(rindex, SolutionAtNode );
  }*/

template<class TMovingImage,class TFixedImage>
typename FEMRegistrationFilter<TMovingImage , TFixedImage>::FloatImageType* 
FEMRegistrationFilter<TMovingImage,TFixedImage>::GetMetricImage(FieldType* Field)
{
  typename FloatImageType::RegionType metricRegion;

  metricRegion.SetSize( m_MovingImage->GetLargestPossibleRegion().GetSize() );
  if (m_FloatImage == NULL){
  m_FloatImage = FloatImageType::New();
  m_FloatImage->SetLargestPossibleRegion( metricRegion );
  m_FloatImage->SetBufferedRegion( metricRegion );
  m_FloatImage->Allocate(); 
  }
  
  m_Load=FEMRegistrationFilter<TMovingImage,TFixedImage>::ImageMetricLoadType::New();
  m_Load->SetMovingImage(m_MovingImage);
  m_Load->SetFixedImage(m_FixedImage);
  if (!m_Field) this->InitializeField();
  m_Load->SetDeformationField(this->GetDeformationField());
  m_Load->SetMetric(m_Metric);
  m_Load->InitializeMetric();
  m_Load->SetTemp(m_Temp);
  m_Load->SetGamma(m_Gamma[m_CurrentLevel]);
  typename ImageType::SizeType r;
  for (unsigned int i=0; i < ImageDimension; i++) r[i]=m_MetricWidth[m_CurrentLevel];
  m_Load->SetMetricRadius(r);
  m_Load->SetNumberOfIntegrationPoints(m_NumberOfIntegrationPoints[m_CurrentLevel]);
  m_Load->SetSign((Float)m_DescentDirection);
 
   
  FieldIterator m_FieldIter( m_Field, m_FieldRegion );
  m_FieldIter.GoToBegin();
  typename ImageType::IndexType rindex = m_FieldIter.GetIndex();

  
  std:: cout << " get metric image " << std::endl;
  for( ; !m_FieldIter.IsAtEnd(); ++m_FieldIter )
    {
    rindex=m_FieldIter.GetIndex();
    typename FieldType::PixelType vector=m_Field->GetPixel(rindex);

    typename ImageMetricLoadType::VectorType invec;
    invec.resize(ImageDimension*2);
    for (unsigned int i=0; i<ImageDimension; i++)
      {
      invec[i]=(double) rindex[i];
      invec[i+ImageDimension]=(double) vector[i];
      }
    typename FloatImageType::PixelType mPix=m_Load->GetMetric(invec);
    
    m_FloatImage->SetPixel(rindex, mPix );

    }
  
  return m_FloatImage;

}

template<class TMovingImage,class TFixedImage>
void FEMRegistrationFilter<TMovingImage,TFixedImage>::ComputeJacobian( float sign, FieldType* field)
{
  unsigned int row; 
  unsigned int col;
  bool jproduct=true;
  typename FloatImageType::RegionType m_JacobianRegion;

  if (!field) 
  { 
    field=m_Field;
    jproduct=false;
  }
  ImageSizeType s= field->GetLargestPossibleRegion().GetSize();
//  std::cout << " size " << s << std::endl;
  m_JacobianRegion.SetSize(s);
  bool fullfield=true;
  for (row=0; row<ImageDimension; row++)
  {
    if (s[row] != m_FullImageSize[row] ) fullfield=false;
  }
  if (!fullfield) jproduct=false;

  if (fullfield && jproduct && !m_FloatImage)
  {
  m_FloatImage = FloatImageType::New();
  m_FloatImage->SetLargestPossibleRegion( field->GetLargestPossibleRegion() );
  m_FloatImage->SetBufferedRegion( field->GetLargestPossibleRegion().GetSize() );
  m_FloatImage->Allocate(); 
  
  ImageRegionIteratorWithIndex<FloatImageType> wimIter( m_FloatImage, m_FloatImage->GetLargestPossibleRegion()  );
  wimIter.GoToBegin();  
  for( ; !wimIter.IsAtEnd(); ++wimIter ) wimIter.Set(1.0);
  
  }
  
 

  typename Element::MatrixType jMatrix,idMatrix;
  jMatrix.resize(ImageDimension,ImageDimension);
  
  FieldIterator m_FieldIter( field, field->GetLargestPossibleRegion() );
  typename ImageType::IndexType rindex;
  typename ImageType::IndexType ddrindex;
  typename ImageType::IndexType ddlindex;

  typename ImageType::IndexType difIndex[ImageDimension][2];
  
  std:: cout << " get jacobian " << std::endl;

  double det;
  unsigned int posoff=1;

  float space=1.0;

    typename FieldType::PixelType dPix;
    typename FieldType::PixelType lpix;
    typename FieldType::PixelType llpix;
    typename FieldType::PixelType rpix;
    typename FieldType::PixelType rrpix;
    typename FieldType::PixelType cpix;

  for(  m_FieldIter.GoToBegin() ; !m_FieldIter.IsAtEnd(); ++m_FieldIter )
  {
    rindex=m_FieldIter.GetIndex();
    float mindist=3.0;
    bool oktosample=true;
    float dist;
    for (row=0; row<ImageDimension; row++)
    {
      dist=fabs((float)rindex[row]);      
      if (dist < mindist) oktosample=false;
      dist = fabs((float)s[row]-(float)rindex[row]);
      if (dist < mindist) oktosample=false;
    }

    cpix=field->GetPixel(rindex);
    Float dV;
    for(row=0; row< ImageDimension;row++)
      {
      difIndex[row][0]=rindex;
      difIndex[row][1]=rindex;
      ddrindex=rindex;
      ddlindex=rindex;
      if (rindex[row] < m_FieldSize[row]-2) 
        {
        difIndex[row][0][row]=rindex[row]+posoff;
        ddrindex[row]=rindex[row]+posoff*2;
        }
      if (rindex[row] > 1 )
        {
        difIndex[row][1][row]=rindex[row]-1;
        ddlindex[row]=rindex[row]-2;
        }
//      std::cout << " indices " << difIndex[row][0] << " " <<difIndex[row][1] << std::endl;

      float h=0.5;
      space=1.0; // should use image spacing here?

      rpix = field->GetPixel(difIndex[row][1]);
      rpix = rpix*h+cpix*(1.-h);
      lpix = field->GetPixel(difIndex[row][0]);
      lpix = lpix*h+cpix*(1.-h);
      dPix = ( rpix - lpix)*sign*space/(2.0);
/*
      rrpix = field->GetPixel(ddrindex);
      rrpix = rrpix*h+rpix*(1.-h);
      llpix = field->GetPixel(ddlindex);
      llpix = llpix*h+lpix*(1.-h);
      dPix=( rpix*8.0 - lpix*8.0 + llpix - rrpix )*sign*space/(12.0); //4th order centered difference
*/
      for(col=0; col< ImageDimension;col++){
        Float val;
        if (row == col) val=dPix[col]+1.0;
        else val = dPix[col];
//        std::cout << " row " << row << " col " << col << " val " << val << std::endl;
        jMatrix.put(row,col,val);
        }
      }
    
    //the determinant of the jacobian matrix
    // std::cout << " get det " << std::endl;
    det = vnl_determinant(jMatrix);
    if ( jproduct  )  // FIXME - NEED TO COMPOSE THE FULL FIELD
      m_FloatImage->SetPixel(rindex,  m_FloatImage->GetPixel(rindex)*det );
//  
    if ( oktosample)
    {
    if (det < m_MinJacobian) m_MinJacobian=det;
    }
  }
//  std:: cout << " mat val " << jMatrix << " det " << det << std::endl;

  if (jproduct)
  {
  typedef DiscreteGaussianImageFilter<FloatImageType, FloatImageType> dgf;

  typename dgf::Pointer filter = dgf::New();
  float sig=1.5;
  filter->SetVariance(sig);
  filter->SetMaximumError(.01f);
  filter->SetInput(m_FloatImage);
  filter->Update();
  m_FloatImage=filter->GetOutput();
  }

}



template<class TMovingImage,class TFixedImage>
void FEMRegistrationFilter<TMovingImage,TFixedImage>::EnforceDiffeomorphism(float thresh, SolverType& mySolver )
{
 
 // FIX ME - WE NEED TO STORE THE PRODUCTS OF THE JACOBIANS 
 //          s.t. WE TRACK THE JACOBIAN OF THE O.D.E. FLOW.
  std::cout << " Checking Jacobian " ;  
  this->ComputeJacobian(-1.);
  std::cout << " min Jacobian " << m_MinJacobian << std::endl;
  if (m_MinJacobian < thresh)
  {
  std::cout << " Enforcing diffeomorphism " << std::endl;
  // resize the vector field to full size
    ImageSizeType movingsize=m_MovingImage->GetLargestPossibleRegion().GetSize();

    typename FieldType::Pointer fullField=NULL;
    ExpandFactorsType expandFactors[ImageDimension];
    bool resize=false;
    for (unsigned int ef=0; ef<ImageDimension; ef++) 
    {
      ExpandFactorsType factor=(ExpandFactorsType)((float) movingsize[ef]/(float)m_CurrentLevelImageSize[ef]);
      expandFactors[ef]=factor;
      if (factor != 1.) resize=true;
    }
    if (resize)  
       fullField=ExpandVectorField(expandFactors,m_Field); // this works - linear interp and expansion of vf
    
     
    this->ComputeJacobian(-1.,fullField);
 
 // FIXME : SHOULD COMPUTE THE JACOBIAN AGAIN AND EXIT THE FUNCTION
 // IF IT'S NOT BELOW THE THRESH.  ALSO, WE SHOULD STORE THE FULL TIME 
 // INTEGRATED JACOBIAN FIELD AND MULTIPLY IT BY THE INCREMENTAL.

  // here's where we warp the image

  typedef itk::WarpImageFilter<ImageType,ImageType,FieldType> WarperType;
  typename WarperType::Pointer warper = WarperType::New();
  typedef typename WarperType::CoordRepType CoordRepType;
  typedef itk::NearestNeighborInterpolateImageFunction<ImageType,CoordRepType>
      InterpolatorType0;
  typedef itk::LinearInterpolateImageFunction<ImageType,CoordRepType>
      InterpolatorType1;
  typename InterpolatorType1::Pointer interpolator = InterpolatorType1::New();
  
    warper = WarperType::New();
    warper->SetInput( m_MovingImage );
    warper->SetDeformationField( fullField );
    warper->SetInterpolator( interpolator );
    warper->SetOutputSpacing( m_FixedImage->GetSpacing() );
    warper->SetOutputOrigin( m_FixedImage->GetOrigin() );
    typename ImageType::PixelType padValue = 0;
    warper->SetEdgePaddingValue( padValue );
    warper->Update();

  // set it as the new moving image

  this->SetMovingImage( warper->GetOutput() );


  // if using landmarks, warp them

   if (m_UseLandmarks)
   {
    std::cout << " warping landmarks " << m_LandmarkArray.size() << std::endl;
           
     if(!m_LandmarkArray.empty())
     {
     for(int lmind=0; lmind<m_LandmarkArray.size(); lmind++) 
     {
       std::cout << " source " << m_LandmarkArray[lmind]->GetSource() << " target " << m_LandmarkArray[lmind]->GetTarget() << std::endl;
    
       // Convert the source to warped coords.
       m_LandmarkArray[lmind]->GetSource()=m_LandmarkArray[lmind]->GetSource()+
         (dynamic_cast<LoadLandmark*>( &*mySolver.load.Find(lmind) )->GetForce() );        
       std::cout << " source " << m_LandmarkArray[lmind]->GetSource() << " target " << m_LandmarkArray[lmind]->GetTarget() << std::endl;
       
       LoadLandmark::Pointer l5=(dynamic_cast<LoadLandmark::Pointer>(m_LandmarkArray[lmind]->Clone()));
       mySolver.load.push_back(FEMP<Load>(l5));

     }
     std::cout << " warping landmarks done " << std::endl;
     } else std::cout << " landmark array empty " << std::endl;
  }
  // now repeat the pyramid if necessary
 
  if ( resize )
  {
//lark
  std::cout << " re-doing pyramid " << std::endl;
    typedef itk::CastImageFilter<ImageType,FloatImageType> CasterType1;
    typename CasterType1::Pointer MovingCaster1 = CasterType1::New();
    
    MovingCaster1->SetInput(m_MovingImage); 
    MovingCaster1->Update();
    m_MovingPyramid->SetInput( MovingCaster1->GetOutput());
    m_MovingPyramid->GetOutput( m_CurrentLevel )->Update();

    typedef itk::CastImageFilter<FloatImageType,ImageType> CasterType2;
    typename CasterType2::Pointer MovingCaster2 = CasterType2::New();
    MovingCaster2->SetInput(m_MovingPyramid->GetOutput(m_CurrentLevel)); 
    MovingCaster2->Update();
    m_Load->SetMovingImage(MovingCaster2->GetOutput()); 


  std::cout << " re-doing pyramid done " << std::endl;
  }        

  // store the total deformation by composing with the full field
  if (!m_TotalField) 
  {
    std::cout << " allocating total deformation field " << std::endl;

    m_TotalField = FieldType::New();

    m_FieldRegion.SetSize(m_CurrentLevelImageSize );
    m_TotalField->SetLargestPossibleRegion( m_FieldRegion );
    m_TotalField->SetBufferedRegion( m_FieldRegion );
    m_TotalField->SetLargestPossibleRegion( m_FieldRegion );
    m_TotalField->Allocate(); 
 
    VectorType disp;  
    for (unsigned int t=0; t<ImageDimension; t++) disp[t]=0.0;
    FieldIterator m_FieldIter( m_TotalField, m_FieldRegion );
    m_FieldIter.GoToBegin();
    for( ; !m_FieldIter.IsAtEnd(); ++m_FieldIter )
    {
      m_FieldIter.Set(disp);
    }
  }
  if (m_TotalField)
  {

  typename ImageType::IndexType index;
  FieldIterator m_FieldIter( m_TotalField, m_TotalField->GetLargestPossibleRegion() );
  m_FieldIter.GoToBegin();
  while( !m_FieldIter.IsAtEnd()  )
  {
    index=m_FieldIter.GetIndex();
    m_FieldIter.Set(m_FieldIter.Get()+m_Field->GetPixel(index));
    ++m_FieldIter; 
  }

  }

  // then we set the field to zero


  FieldIterator m_FieldIter( m_Field, m_Field->GetLargestPossibleRegion() );
  m_FieldIter.GoToBegin();
  while( !m_FieldIter.IsAtEnd()  )
  {
    VectorType disp;
    disp.Fill(0.0);  
    m_FieldIter.Set(disp);
    ++m_FieldIter; 
  }

// now do the same for the solver
  unsigned int ii;
  Node::ArrayType* nodes = &(mySolver.node);
  for(  Node::ArrayType::iterator node=nodes->begin(); node!=nodes->end(); node++) 
  {
    // Now put it into the solution!
    for (ii=0; ii < ImageDimension; ii++)
    { 
      mySolver.GetLinearSystemWrapper()->
        SetSolutionValue((*node)->GetDegreeOfFreedom(ii),0.0,mySolver.TotalSolutionIndex);    
      mySolver.GetLinearSystemWrapper()->
        SetSolutionValue((*node)->GetDegreeOfFreedom(ii),0.0,mySolver.SolutionTMinus1Index);    
    }
  }
  
  std::cout << " Enforcing diffeomorphism done "  << std::endl;

  }
  else   std::cout << " Jacobian OK " << std::endl;
  m_MinJacobian=1.0;

}

template<class TMovingImage,class TFixedImage>
void FEMRegistrationFilter<TMovingImage,TFixedImage>::WriteWarpedImage(const char* fname)
{

  // for image output
  std::ofstream fbin; 
  std::string exte=".img";
  std::string fnum;
  m_FileCount++;

  OStringStream buf;
  buf<<(m_FileCount+10);
  fnum=std::string(buf.str().c_str());

  std::string fullfname=(fname+fnum+exte);

  ImageIterator wimIter( m_WarpedImage,m_Wregion );

  fbin.open(fullfname.c_str(),std::ios::out | std::ios::binary);
  ImageDataType t=0;
  // for arbitrary dimensionality
  wimIter.GoToBegin();  
  for( ; !wimIter.IsAtEnd(); ++wimIter )
    {
    t=(ImageDataType) wimIter.Get();
    fbin.write(reinterpret_cast<const char*>(&t),sizeof(ImageDataType));
    }
  
}


template<class TMovingImage,class TFixedImage>
typename FEMRegistrationFilter<TMovingImage,TFixedImage>::FieldType::Pointer 
FEMRegistrationFilter<TMovingImage,TFixedImage>::ExpandVectorField( ExpandFactorsType* expandFactors, FieldType* field)
{
// re-size the vector field
  if (!field) field=m_Field;

  std::cout << " input field size " << m_Field->GetLargestPossibleRegion().GetSize()
    << " expand factors ";
  VectorType pad;
  for (int i=0; i< ImageDimension; i++) 
  {
    pad[i]=0.0;
    std::cout << expandFactors[i] << " ";
  }
  std::cout << std::endl; 
  typename ExpanderType::Pointer m_FieldExpander = ExpanderType::New(); 
  m_FieldExpander->SetInput(field);
  m_FieldExpander->SetExpandFactors( expandFactors );
// use default    
  m_FieldExpander->SetEdgePaddingValue( pad );
  m_FieldExpander->UpdateLargestPossibleRegion();

  m_FieldSize=m_FieldExpander->GetOutput()->GetLargestPossibleRegion().GetSize();

  return m_FieldExpander->GetOutput();
  std::cout << " resized field to " << m_FieldSize << std::endl;

}

template<class TMovingImage,class TFixedImage>
void FEMRegistrationFilter<TMovingImage,TFixedImage>::SampleVectorFieldAtNodes(SolverType& mySolver)
{

//  std::cout << " upsampling vector field " << std::endl;

  // Here, we need to iterate through the nodes, get the nodal coordinates,
  // sample the VF at the node and place the values in the SolutionVector.
  unsigned int ii;
  Node::ArrayType* nodes = &(mySolver.node);
  Element::VectorType coord;  
  VectorType SolutionAtNode;

  typename ImageType::IndexType rindex;
  

  for(  Node::ArrayType::iterator node=nodes->begin(); node!=nodes->end(); node++) 
  {
    coord=(*node)->GetCoordinates();
    bool inimage=true;
    for (ii=0; ii < ImageDimension; ii++)
    { 
      if (coord[ii] != 0) 
        rindex[ii]=(long int) ((Float)coord[ii]+0.5);
      else 
        rindex[ii]=0;
      if( (long int)rindex[ii]  < (long int)0 || 
          (long int)rindex[ii]  > (long int)m_FieldSize[ii]-1 )
      { 
        inimage=false;      
      }

      SolutionAtNode[ii]=0;
    }
   
    if (inimage)   SolutionAtNode=m_Field->GetPixel(rindex);

    // Now put it into the solution!
    for (ii=0; ii < ImageDimension; ii++)
    { 
      Float Sol=SolutionAtNode[ii];///((Float)m_ImageScaling[ii]); // Scale back to current scale

//      std::cout << " the sol " << Sol << " at coord " << coord << " rind " << rindex << std::endl;
      mySolver.GetLinearSystemWrapper()->
        SetSolutionValue((*node)->GetDegreeOfFreedom(ii),Sol,mySolver.TotalSolutionIndex);    
      mySolver.GetLinearSystemWrapper()->
        SetSolutionValue((*node)->GetDegreeOfFreedom(ii),Sol,mySolver.SolutionTMinus1Index);    
    }
  }

}


template<class TMovingImage,class TFixedImage>
void FEMRegistrationFilter<TMovingImage,TFixedImage>::PrintVectorField(unsigned int modnum)
{
  FieldIterator m_FieldIter( m_Field, m_FieldRegion );
  m_FieldIter.GoToBegin();
  unsigned int ct=0;

  float max=0;
  while( !m_FieldIter.IsAtEnd()  )
  {
    VectorType disp=m_FieldIter.Get();  
    //for (unsigned int i=0; i<ImageDimension; i++) disp[i]=-9.9;
    if ((ct % modnum) == 0)  std::cout << " field pix " << m_FieldIter.Get() << std::endl;
    for (int i=0; i<ImageDimension;i++)
      if (fabs(disp[i]) > max ) max=fabs(disp[i]);
    ++m_FieldIter; 
    ct++;

  }
  std::cout << " max  vec " << max << std::endl;
}



template<class TMovingImage,class TFixedImage>
void FEMRegistrationFilter<TMovingImage,TFixedImage>::MultiResSolve()
{

//  Float LastScaleEnergy=0.0, ThisScaleEnergy=0.0;
  vnl_vector<Float> LastResolutionSolution;
//   Setup a multi-resolution pyramid
  typedef typename m_FixedPyramidType::ScheduleType ScheduleType;
  m_MovingPyramid = m_FixedPyramidType::New();
  m_FixedPyramid = m_FixedPyramidType::New();

  typedef itk::CastImageFilter<ImageType,FloatImageType> CasterType1;
  typedef itk::CastImageFilter<FloatImageType,ImageType> CasterType2;
  typename CasterType1::Pointer MovingCaster1 = CasterType1::New();
  typename CasterType1::Pointer FixedCaster1 = CasterType1::New();
  typename CasterType2::Pointer MovingCaster2;
  typename CasterType2::Pointer FixedCaster2;
//lark2
  MovingCaster1->SetInput(m_MovingImage); MovingCaster1->Update();
  m_MovingPyramid->SetInput( MovingCaster1->GetOutput());
  FixedCaster1->SetInput(m_FixedImage); FixedCaster1->Update();
  m_FixedPyramid->SetInput( FixedCaster1->GetOutput());

// set schedule by specifying the number of levels;
  m_MovingPyramid->SetNumberOfLevels( m_NumLevels );
  m_FixedPyramid->SetNumberOfLevels( m_NumLevels );

  ScheduleType SizeReductionMoving=m_MovingPyramid->GetSchedule();
  ScheduleType SizeReductionFixed=m_FixedPyramid->GetSchedule();
  
  for (unsigned int ii=0; ii<m_NumLevels; ii++) 
    for (unsigned int jj=0; jj<ImageDimension; jj++) 
      { 
      unsigned int scale=m_ImageScaling[jj]/(unsigned int)pow(2.0,(double)ii);
      if (scale < 1) scale=1;
      SizeReductionMoving[ii][jj]=scale;
      SizeReductionFixed[ii][jj]=scale;
      }
      
  m_MovingPyramid->SetSchedule(SizeReductionMoving); 
  m_FixedPyramid->SetSchedule(SizeReductionFixed);
  std::cout << " size change at each level " << SizeReductionMoving << std::endl;
//  m_MovingPyramid->Update();
//  m_FixedPyramid->Update();
  
  for (m_CurrentLevel=0; m_CurrentLevel<m_MaxLevel; m_CurrentLevel++)
  {
  
    m_MovingPyramid->GetOutput( m_CurrentLevel )->Update();
    m_FixedPyramid->GetOutput( m_CurrentLevel )->Update();
    
    typename ImageType::SizeType nextLevelSize;
    m_CurrentLevelImageSize=m_FixedPyramid->GetOutput( m_CurrentLevel )->GetLargestPossibleRegion().GetSize();
    if (m_CurrentLevel < m_MaxLevel-1)
      nextLevelSize=m_FixedPyramid->GetOutput( m_CurrentLevel+1 )->GetLargestPossibleRegion().GetSize();
    else nextLevelSize=m_FixedPyramid->GetOutput( m_CurrentLevel )->GetLargestPossibleRegion().GetSize();

    double scaling[ImageDimension];
    for (unsigned int d=0; d < ImageDimension; d++)
      {
      m_ImageScaling[d]=SizeReductionMoving[m_CurrentLevel][d];
      if (m_CurrentLevel == 0) scaling[d]=(double)SizeReductionMoving[m_CurrentLevel][d];
      else scaling[d]=
       (double) m_FixedPyramid->GetOutput( m_CurrentLevel-1 )->GetLargestPossibleRegion().GetSize()[d]/
       (double) m_CurrentLevelImageSize[d];
       std::cout << " scaling " << scaling[d] << std::endl;
      }
    double MeshResolution=(double)this->m_MeshPixelsPerElementAtEachResolution(m_CurrentLevel);


    if (m_Maxiters[m_CurrentLevel] > 0)
    {
      SolverType SSS;
  
      MovingCaster2 = CasterType2::New();// Weird - don't know why but this worked
      FixedCaster2 = CasterType2::New();// and declaring the Casters outside the loop did not.

      MovingCaster2 = CasterType2::New();// Weird - don't know why but this worked
      FixedCaster2 = CasterType2::New();// and declaring the Casters outside the loop did not.
      MovingCaster2->SetInput(m_MovingPyramid->GetOutput(m_CurrentLevel)); 
      MovingCaster2->Update(); 
      FixedCaster2->SetInput(m_FixedPyramid->GetOutput(m_CurrentLevel)); 
      FixedCaster2->Update();


      SSS.SetDeltatT(m_dT);  
      SSS.SetRho(m_Rho[m_CurrentLevel]);     
      SSS.SetAlpha(m_Alpha);    

      CreateMesh(MeshResolution,SSS,m_CurrentLevelImageSize); 
      ApplyLoads(SSS,m_CurrentLevelImageSize,scaling);
      ApplyImageLoads(SSS,MovingCaster2->GetOutput(),FixedCaster2->GetOutput());

      unsigned int ndofpernode=(m_Element)->GetNumberOfDegreesOfFreedomPerNode();
      unsigned int numnodesperelt=(m_Element)->GetNumberOfNodes()+1;
      unsigned int ndof=SSS.GetNumberOfDegreesOfFreedom();
      unsigned int nzelts;
      if (!m_ReadMeshFile) nzelts=numnodesperelt*ndofpernode*ndof; 
        else nzelts=((2*numnodesperelt*ndofpernode*ndof > 25*ndof) ? 2*numnodesperelt*ndofpernode*ndof : 25*ndof);


      LinearSystemWrapperItpack itpackWrapper; 
      unsigned int maxits=2*SSS.GetNumberOfDegreesOfFreedom();
      itpackWrapper.SetMaximumNumberIterations(maxits); 
      itpackWrapper.SetTolerance(1.e-1);
      itpackWrapper.JacobianConjugateGradient(); 
      itpackWrapper.SetMaximumNonZeroValuesInMatrix(nzelts);
      SSS.SetLinearSystemWrapper(&itpackWrapper); 





        if( m_UseMassMatrix ) SSS.AssembleKandM(); 
        else 
        {
          SSS.InitializeForSolution();
          SSS.AssembleK();
        }
        if (m_CurrentLevel > 0)  this->SampleVectorFieldAtNodes(SSS);
        this->PrintVectorField(900000);
        this->IterativeSolve(SSS);
      }
      else
      {
      }


// now expand the field for the next level, if necessary.
      if ( m_CurrentLevel == m_MaxLevel-1) 
      { 

        // expand the field to full size
        ExpandFactorsType expandFactors[ImageDimension];
//         bool NeedToExpand=false;
        for (unsigned int ef=0; ef<ImageDimension; ef++)
        { 
          expandFactors[ef]= (ExpandFactorsType)((float)m_FullImageSize[ef]/(float)m_FieldSize[ef]);
//          if (expandFactors[ef]!=1) NeedToExpand=true;
        }
 
      } 
      else if (m_CurrentLevel < m_MaxLevel-1)
      {
        ExpandFactorsType expandFactors[ImageDimension];
        for (unsigned int ef=0; ef<ImageDimension; ef++) 
        {
            expandFactors[ef]=(ExpandFactorsType)
           ((float) nextLevelSize[ef]/(float)m_CurrentLevelImageSize[ef]);
//          std::cout << " exp f " <<expandFactors[ef] << std::endl;
        }
        m_Field=ExpandVectorField(expandFactors,m_Field); // this works - linear interp and expansion of vf
        if (m_TotalField) m_TotalField=ExpandVectorField(expandFactors,m_TotalField);
      } 
      
      PrintVectorField(900000);
      std:: cout << " field size " << m_FieldSize << std::endl;
    

       
    }// end image resolution loop


  return;
  
}

template<class TMovingImage,class TFixedImage>
Element::Float FEMRegistrationFilter<TMovingImage,TFixedImage>::EvaluateResidual(SolverType& mySolver,Float t)
{
  
  //Float defe=mySolver.GetDeformationEnergy(t);
  Float SimE=m_Load->EvaluateMetricGivenSolution(&(mySolver.el),t);

  Float maxsim=1.0;
  for (unsigned int i=0; i< ImageDimension; i++) maxsim*=(Float)m_FullImageSize[i];
  if ( m_WhichMetric != 0) SimE=maxsim-SimE;
  //std::cout << " SimE " << SimE << " Def E " << defe << std::endl;
  return fabs((double)SimE); //+defe;

}

template<class TMovingImage,class TFixedImage>
void FEMRegistrationFilter<TMovingImage,TFixedImage>::FindBracketingTriplet(SolverType& mySolver,Float* a, Float* b, Float* c)
{
  // in 1-D domain, we want to find a < b < c , s.t.  f(b) < f(a) && f(b) < f(c)
  //  see Numerical Recipes
 
  Float Gold=1.618034;
  Float Glimit=100.0;
  Float Tiny=1.e-20;
  
  Float ax, bx, cx;
  ax=0.0; bx=1.; cx;
  Float fc;
  Float fa=fabs(EvaluateResidual(mySolver, ax));
  Float fb=fabs(EvaluateResidual(mySolver, bx));
  
  Float ulim,u,r,q,fu,dum;

  if ( fb > fa ) 
    {
    dum=ax; ax=bx; bx=dum;
    dum=fb; fb=fa; fa=dum;
    }

  cx=bx+Gold*(bx-ax);  // first guess for c - the 3rd pt needed to bracket the min
  fc=fabs(EvaluateResidual(mySolver, cx));

  
  while (fb > fc  )
  // && fabs(ax) < 3. && fabs(bx) < 3. && fabs(cx) < 3.)
    {
    r=(bx-ax)*(fb-fc);
    q=(bx-cx)*(fb-fa);
    Float denom=(2.0*mySolver.GSSign(mySolver.GSMax(fabs(q-r),Tiny),q-r));
    u=(bx)-((bx-cx)*q-(bx-ax)*r)/denom;
    ulim=bx + Glimit*(cx-bx);
    if ((bx-u)*(u-cx) > 0.0)
      {
      fu=fabs(EvaluateResidual(mySolver, u));
      if (fu < fc)
        {
        ax=bx;
        bx=u;
        *a=ax; *b=bx; *c=cx;
        return;
        }
      else if (fu > fb)
        {
        cx=u;
        *a=ax; *b=bx; *c=cx;
        return;
        }
      
      u=cx+Gold*(cx-bx);
      fu=fabs(EvaluateResidual(mySolver, u));
      
      }
    else if ( (cx-u)*(u-ulim) > 0.0)
      {
      fu=fabs(EvaluateResidual(mySolver, u));
      if (fu < fc)
        {
        bx=cx; cx=u; u=cx+Gold*(cx-bx);
        fb=fc; fc=fu; fu=fabs(EvaluateResidual(mySolver, u));
        }

      }
    else if ( (u-ulim)*(ulim-cx) >= 0.0)
      {
      u=ulim;
      fu=fabs(EvaluateResidual(mySolver, u));
      }
    else
      {
      u=cx+Gold*(cx-bx);
      fu=fabs(EvaluateResidual(mySolver, u));
      }
    
    ax=bx; bx=cx; cx=u;
    fa=fb; fb=fc; fc=fu;

    }

  if ( fabs(ax) > 1.e3  || fabs(bx) > 1.e3 || fabs(cx) > 1.e3)
    {ax=-2.0;  bx=1.0;  cx=2.0; } // to avoid crazy numbers caused by bad bracket (u goes nuts)
  
  *a=ax; *b=bx; *c=cx;
}


template<class TMovingImage,class TFixedImage>
Element::Float FEMRegistrationFilter<TMovingImage,TFixedImage>::GoldenSection(SolverType& mySolver,Float tol,unsigned int MaxIters)
{
  // We should now have a, b and c, as well as f(a), f(b), f(c), 
  // where b gives the minimum energy position;

  Float ax, bx, cx;


  FindBracketingTriplet(mySolver,&ax, &bx, &cx);
  Float xmin,fmin;
  //if (fb!=0.0)
  Float f1,f2,x0,x1,x2,x3;

  Float R=0.6180339;
  Float C=(1.0-R);

  x0=ax;
  x3=cx;
  if (fabs(cx-bx) > fabs(bx-ax))
    {
    x1=bx;
    x2=bx+C*(cx-bx);
    } 
  else
    {
    x2=bx;
    x1=bx-C*(bx-ax);
    }
  f1=fabs(EvaluateResidual(mySolver, x1));
  f2=fabs(EvaluateResidual(mySolver, x2));
  unsigned int iters=0;
  while (fabs(x3-x0) > tol*(fabs(x1)+fabs(x2)) && iters < MaxIters)
    {
    iters++;
    if (f2 < f1)
      {
      x0=x1; x1=x2; x2=R*x1+C*x3;
      f1=f2; f2=fabs(EvaluateResidual(mySolver, x2));
      } 
    else
      {
      x3=x2; x2=x1; x1=R*x2+C*x0;
      f2=f1; f1=fabs(EvaluateResidual(mySolver, x1));
      }
    }
  if (f1<f2)
    {
    xmin=x1; 
    fmin=f1;
    } 
  else
    {
    xmin=x2;
    fmin=f2;
    }

  mySolver.SetEnergyToMin(xmin);
  std:: cout << " emin " << fmin <<  " at xmin " << xmin << std::endl;
  return fabs((double)fmin); 
}

template<class TMovingImage,class TFixedImage>
void FEMRegistrationFilter<TMovingImage,TFixedImage>::
PrintSelf(std::ostream& os, Indent indent) const 
{ 
  Superclass::PrintSelf( os, indent );

  if (m_Load)
    {
    os << indent << "Load = " << m_Load;
    }
  else
    {
    os << indent << "Load = " << "(None)" << std::endl;
    }
}

}} // end namespace itk::fem


 
#endif


/*=========================================================================
 *
 *  Copyright Insight Software Consortium
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0.txt
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *=========================================================================*/
#ifndef __itkGeodesicActiveContourShapePriorLevelSetFunction_hxx
#define __itkGeodesicActiveContourShapePriorLevelSetFunction_hxx

#include "itkGeodesicActiveContourShapePriorLevelSetFunction.h"
#include "itkGradientRecursiveGaussianImageFilter.h"
#include "itkImageAlgorithm.h"

namespace itk
{
/**
 * Calculate the speed image.
 */
template< typename TImageType, typename TFeatureImageType >
void
GeodesicActiveContourShapePriorLevelSetFunction< TImageType, TFeatureImageType >
::CalculateSpeedImage()
{
  /* copy the feature image into the speed image */
  ImageAlgorithm::Copy( this->GetFeatureImage(),
                        this->GetSpeedImage(),
                        this->GetFeatureImage()->GetRequestedRegion(),
                        this->GetFeatureImage()->GetRequestedRegion() );
}

/**
 * Calculate the advection speed image
 */
template< typename TImageType, typename TFeatureImageType >
void GeodesicActiveContourShapePriorLevelSetFunction< TImageType, TFeatureImageType >
::CalculateAdvectionImage()
{
  /* compoute the gradient of the feature image. */
  typedef GradientRecursiveGaussianImageFilter< FeatureImageType, VectorImageType >
  DerivativeFilterType;

  typename DerivativeFilterType::Pointer derivative = DerivativeFilterType::New();
  derivative->SetInput( this->GetFeatureImage() );
  derivative->SetSigma(m_DerivativeSigma);
  derivative->Update();

  /* copy negative gradient into the advection image. */
  ImageRegionIterator< VectorImageType >
  dit( derivative->GetOutput(), this->GetFeatureImage()->GetRequestedRegion() );
  ImageRegionIterator< VectorImageType >
  ait( this->GetAdvectionImage(), this->GetFeatureImage()->GetRequestedRegion() );

  for ( dit.GoToBegin(), ait.GoToBegin(); !dit.IsAtEnd(); ++dit, ++ait )
    {
    typename VectorImageType::PixelType v = dit.Get();
    for ( unsigned int j = 0; j < ImageDimension; j++ )
      {
      v[j] *= -1.0L;
      }
    ait.Set(v);
    }
}
} // end namespace itk

#endif

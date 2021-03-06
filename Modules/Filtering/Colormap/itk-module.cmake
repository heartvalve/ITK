set(DOCUMENTATION "This module contains filter/functions for converting
grayscale images to colormapped rgb images.")

itk_module(ITKColormap
  DEPENDS
    ITKCommon
  TEST_DEPENDS
    ITKTestKernel
  DESCRIPTION
    "${DOCUMENTATION}"
)

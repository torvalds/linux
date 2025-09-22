//===-- xray-graph.cpp: XRay Function Call Graph Renderer -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A class to get a color from a specified gradient.
//
//===----------------------------------------------------------------------===//

#include "xray-color-helper.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/raw_ostream.h"
#include <cmath>

using namespace llvm;
using namespace xray;

//  Sequential ColorMaps, which are used to represent information
//  from some minimum to some maximum.

const std::tuple<uint8_t, uint8_t, uint8_t> SequentialMaps[][9] = {
    {// The greys color scheme from http://colorbrewer2.org/
     std::make_tuple(255, 255, 255), std::make_tuple(240, 240, 240),
     std::make_tuple(217, 217, 217), std::make_tuple(189, 189, 189),
     std::make_tuple(150, 150, 150), std::make_tuple(115, 115, 115),
     std::make_tuple(82, 82, 82), std::make_tuple(37, 37, 37),
     std::make_tuple(0, 0, 0)},
    {// The OrRd color scheme from http://colorbrewer2.org/
     std::make_tuple(255, 247, 236), std::make_tuple(254, 232, 200),
     std::make_tuple(253, 212, 158), std::make_tuple(253, 187, 132),
     std::make_tuple(252, 141, 89), std::make_tuple(239, 101, 72),
     std::make_tuple(215, 48, 31), std::make_tuple(179, 0, 0),
     std::make_tuple(127, 0, 0)},
    {// The PuBu color scheme from http://colorbrewer2.org/
     std::make_tuple(255, 247, 251), std::make_tuple(236, 231, 242),
     std::make_tuple(208, 209, 230), std::make_tuple(166, 189, 219),
     std::make_tuple(116, 169, 207), std::make_tuple(54, 144, 192),
     std::make_tuple(5, 112, 176), std::make_tuple(4, 90, 141),
     std::make_tuple(2, 56, 88)}};

// Sequential Maps extend the last colors given out of range inputs.
const std::tuple<uint8_t, uint8_t, uint8_t> SequentialBounds[][2] = {
    {// The Bounds for the greys color scheme
     std::make_tuple(255, 255, 255), std::make_tuple(0, 0, 0)},
    {// The Bounds for the OrRd color Scheme
     std::make_tuple(255, 247, 236), std::make_tuple(127, 0, 0)},
    {// The Bounds for the PuBu color Scheme
     std::make_tuple(255, 247, 251), std::make_tuple(2, 56, 88)}};

ColorHelper::ColorHelper(ColorHelper::SequentialScheme S)
    : MinIn(0.0), MaxIn(1.0), ColorMap(SequentialMaps[static_cast<int>(S)]),
      BoundMap(SequentialBounds[static_cast<int>(S)]) {}

// Diverging ColorMaps, which are used to represent information
// representing differenes, or a range that goes from negative to positive.
// These take an input in the range [-1,1].

const std::tuple<uint8_t, uint8_t, uint8_t> DivergingCoeffs[][11] = {
    {// The PiYG color scheme from http://colorbrewer2.org/
     std::make_tuple(142, 1, 82), std::make_tuple(197, 27, 125),
     std::make_tuple(222, 119, 174), std::make_tuple(241, 182, 218),
     std::make_tuple(253, 224, 239), std::make_tuple(247, 247, 247),
     std::make_tuple(230, 245, 208), std::make_tuple(184, 225, 134),
     std::make_tuple(127, 188, 65), std::make_tuple(77, 146, 33),
     std::make_tuple(39, 100, 25)}};

// Diverging maps use out of bounds ranges to show missing data. Missing Right
// Being below min, and missing left being above max.
const std::tuple<uint8_t, uint8_t, uint8_t> DivergingBounds[][2] = {
    {// The PiYG color scheme has green and red for missing right and left
     // respectively.
     std::make_tuple(255, 0, 0), std::make_tuple(0, 255, 0)}};

ColorHelper::ColorHelper(ColorHelper::DivergingScheme S)
    : MinIn(-1.0), MaxIn(1.0), ColorMap(DivergingCoeffs[static_cast<int>(S)]),
      BoundMap(DivergingBounds[static_cast<int>(S)]) {}

// Takes a tuple of uint8_ts representing a color in RGB and converts them to
// HSV represented by a tuple of doubles
static std::tuple<double, double, double>
convertToHSV(const std::tuple<uint8_t, uint8_t, uint8_t> &Color) {
  double Scaled[3] = {std::get<0>(Color) / 255.0, std::get<1>(Color) / 255.0,
                      std::get<2>(Color) / 255.0};
  int Min = 0;
  int Max = 0;
  for (int i = 1; i < 3; ++i) {
    if (Scaled[i] < Scaled[Min])
      Min = i;
    if (Scaled[i] > Scaled[Max])
      Max = i;
  }

  double C = Scaled[Max] - Scaled[Min];

  double HPrime =
      (C == 0) ? 0 : (Scaled[(Max + 1) % 3] - Scaled[(Max + 2) % 3]) / C;
  HPrime = HPrime + 2.0 * Max;

  double H = (HPrime < 0) ? (HPrime + 6.0) * 60
                          : HPrime * 60; // Scale to between 0 and 360
  double V = Scaled[Max];

  double S = (V == 0.0) ? 0.0 : C / V;

  return std::make_tuple(H, S, V);
}

// Takes a double precision number, clips it between 0 and 1 and then converts
// that to an integer between 0x00 and 0xFF with proxpper rounding.
static uint8_t unitIntervalTo8BitChar(double B) {
  double n = std::clamp(B, 0.0, 1.0);
  return static_cast<uint8_t>(255 * n + 0.5);
}

// Takes a typle of doubles representing a color in HSV and converts them to
// RGB represented as a tuple of uint8_ts
static std::tuple<uint8_t, uint8_t, uint8_t>
convertToRGB(const std::tuple<double, double, double> &Color) {
  const double &H = std::get<0>(Color);
  const double &S = std::get<1>(Color);
  const double &V = std::get<2>(Color);

  double C = V * S;

  double HPrime = H / 60;
  double X = C * (1 - std::abs(std::fmod(HPrime, 2.0) - 1));

  double RGB1[3];
  int HPrimeInt = static_cast<int>(HPrime);
  if (HPrimeInt % 2 == 0) {
    RGB1[(HPrimeInt / 2) % 3] = C;
    RGB1[(HPrimeInt / 2 + 1) % 3] = X;
    RGB1[(HPrimeInt / 2 + 2) % 3] = 0.0;
  } else {
    RGB1[(HPrimeInt / 2) % 3] = X;
    RGB1[(HPrimeInt / 2 + 1) % 3] = C;
    RGB1[(HPrimeInt / 2 + 2) % 3] = 0.0;
  }

  double Min = V - C;
  double RGB2[3] = {RGB1[0] + Min, RGB1[1] + Min, RGB1[2] + Min};

  return std::make_tuple(unitIntervalTo8BitChar(RGB2[0]),
                         unitIntervalTo8BitChar(RGB2[1]),
                         unitIntervalTo8BitChar(RGB2[2]));
}

// The Hue component of the HSV interpolation Routine
static double interpolateHue(double H0, double H1, double T) {
  double D = H1 - H0;
  if (H0 > H1) {
    std::swap(H0, H1);

    D = -D;
    T = 1 - T;
  }

  if (D <= 180) {
    return H0 + T * (H1 - H0);
  } else {
    H0 = H0 + 360;
    return std::fmod(H0 + T * (H1 - H0) + 720, 360);
  }
}

// Interpolates between two HSV Colors both represented as a tuple of doubles
// Returns an HSV Color represented as a tuple of doubles
static std::tuple<double, double, double>
interpolateHSV(const std::tuple<double, double, double> &C0,
               const std::tuple<double, double, double> &C1, double T) {
  double H = interpolateHue(std::get<0>(C0), std::get<0>(C1), T);
  double S = std::get<1>(C0) + T * (std::get<1>(C1) - std::get<1>(C0));
  double V = std::get<2>(C0) + T * (std::get<2>(C1) - std::get<2>(C0));
  return std::make_tuple(H, S, V);
}

// Get the Color as a tuple of uint8_ts
std::tuple<uint8_t, uint8_t, uint8_t>
ColorHelper::getColorTuple(double Point) const {
  assert(!ColorMap.empty() && "ColorMap must not be empty!");
  assert(!BoundMap.empty() && "BoundMap must not be empty!");

  if (Point < MinIn)
    return BoundMap[0];
  if (Point > MaxIn)
    return BoundMap[1];

  size_t MaxIndex = ColorMap.size() - 1;
  double IntervalWidth = MaxIn - MinIn;
  double OffsetP = Point - MinIn;
  double SectionWidth = IntervalWidth / static_cast<double>(MaxIndex);
  size_t SectionNo = std::floor(OffsetP / SectionWidth);
  double T = (OffsetP - SectionNo * SectionWidth) / SectionWidth;

  auto &RGBColor0 = ColorMap[SectionNo];
  auto &RGBColor1 = ColorMap[std::min(SectionNo + 1, MaxIndex)];

  auto HSVColor0 = convertToHSV(RGBColor0);
  auto HSVColor1 = convertToHSV(RGBColor1);

  auto InterpolatedHSVColor = interpolateHSV(HSVColor0, HSVColor1, T);
  return convertToRGB(InterpolatedHSVColor);
}

// A helper method to convert a color represented as tuple of uint8s to a hex
// string.
std::string
ColorHelper::getColorString(std::tuple<uint8_t, uint8_t, uint8_t> t) {
  return std::string(llvm::formatv("#{0:X-2}{1:X-2}{2:X-2}", std::get<0>(t),
                                   std::get<1>(t), std::get<2>(t)));
}

// Gets a color in a gradient given a number in the interval [0,1], it does this
// by evaluating a polynomial which maps [0, 1] -> [0, 1] for each of the R G
// and B values in the color. It then converts this [0,1] colors to a 24 bit
// color as a hex string.
std::string ColorHelper::getColorString(double Point) const {
  return getColorString(getColorTuple(Point));
}

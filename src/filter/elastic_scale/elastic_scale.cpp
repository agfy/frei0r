/*
 * Copyright (C) 2018 Matthias Schnöll (matthias.schnoell AT gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "frei0r.hpp"

// Limits (min/max values) of various data types
#include <limits>
// For the CHAR_BIT constant
#include <climits>
// pow() and other mathematical functions
#include <cmath>

#define PI 3.141592654

/**
This is a frei0r filter which allows to scale video footage non-linearly.
In combination with a linear scale filter, it allows to scale 4:3 footage
to 16:9 and maintain the original aspect ratio in the center part of the image.
*/

typedef struct {
    unsigned int higherXPos;
    unsigned int lowerXPos;
    double lowerWeight;
    double higherWeight;

} TransformationElem;


class ElasticScale : public frei0r::filter
{

public:

    f0r_param_double m_scaleCenter;
    f0r_param_double m_linearScaleArea;
    f0r_param_double m_linearScaleFactor;
    f0r_param_double m_nonLinearScaleFactor;

    ElasticScale(unsigned int width, unsigned int height) : m_width(width), m_height(height)

    {
        register_param(m_scaleCenter,"Center","Horizontal center position of the linear area");
        register_param(m_linearScaleArea,"Linear Width","Width of the linear area");
        register_param(m_linearScaleFactor,"Linear Scale Factor","Amount how much the linear area is scaled");
        register_param(m_nonLinearScaleFactor,"Non-Linear Scale Factor","Amount how much the outer left and outer right areas are scaled non linearly");

        // default values for sinus based scaling
        m_scaleCenter = 0.5;
        m_linearScaleArea = 0.0;
        m_linearScaleFactor = 0.7;
        m_nonLinearScaleFactor = 0.7125;

        updateScalingFactors();
        calcTransformationFactors();
    }

    ~ElasticScale()
    {
        // Delete member variables if necessary.
        delete[] m_transformationCalculations;
    }

    virtual void update(double time,
	                    uint32_t* out,
                        const uint32_t* in)
    {
        if (m_prev_scaleCenter != m_scaleCenter || m_prev_linearScaleArea != m_linearScaleArea || m_prev_linearScaleFactor != m_linearScaleFactor || m_prev_nonLinearScaleFactor != m_nonLinearScaleFactor)
        {
            updateScalingFactors();
            calcTransformationFactors();
        }

        std::fill(&out[0], &out[(int) (width*height)], 0);

        for (int colIdx=0; colIdx<m_width; colIdx++)
        {

            double lowerWeight = m_transformationCalculations[colIdx].lowerWeight;
            double higherWeight = m_transformationCalculations[colIdx].higherWeight;

            for (int rowIdx = 0; rowIdx < m_height; rowIdx++)
            {
                uint32_t newValue = 0;
                unsigned int lowerXPos = rowIdx*m_width+m_transformationCalculations[colIdx].lowerXPos;
                unsigned int higherXPos = rowIdx*m_width+m_transformationCalculations[colIdx].higherXPos;
                unsigned int curPosDst = rowIdx*m_width+colIdx;

                if (higherXPos == lowerXPos) {
                    newValue = in[higherXPos];
                } else {
                    for (int i=3; i>=0; i--)
                    {
                        uint16_t cFold = uint8_t(in[lowerXPos] >> 8*i);
                        uint16_t cCold = uint8_t(in[higherXPos] >> 8*i);
                        cFold = (uint16_t)((double)cFold*(1-lowerWeight));
                        cCold = (uint16_t)((double)cCold*(1-higherWeight));
                        newValue |= ((uint8_t)((cFold + cCold)) << 8*i);
                    }
                }

                out[curPosDst] = newValue;
            }

        }

    }

private:
    
    f0r_param_double m_prev_scaleCenter;
    f0r_param_double m_prev_linearScaleArea;
    f0r_param_double m_prev_linearScaleFactor;
    f0r_param_double m_prev_nonLinearScaleFactor;

    f0r_param_double m_intern_scaleCenter;
    f0r_param_double m_intern_linearScaleArea;
    f0r_param_double m_intern_linearScaleFactor;
    f0r_param_double m_intern_nonLinearScaleFactor;

    unsigned int m_width;
    unsigned int m_height;

    unsigned int m_borderXAbsLeftSrc;
    unsigned int m_borderXAbsRightSrc;

    unsigned int m_borderXAbsLeftDst;
    unsigned int m_borderXAbsRightDst;

    TransformationElem* m_transformationCalculations = NULL;

    void updateScalingFactors()
    {

        // limit input parameters to ranges [0,1]
        if (m_scaleCenter <= 0)
        { m_scaleCenter = 0; }
        else if (m_scaleCenter >= 1)
        { m_scaleCenter = 1; }

        if (m_linearScaleArea <= 0)
        { m_linearScaleArea = 0; }
        else if (m_linearScaleArea >= 1)
        { m_linearScaleArea = 1; }

        if (m_linearScaleFactor <= 0)
        { m_linearScaleFactor = 0; }
        else if (m_linearScaleFactor >= 1)
        { m_linearScaleFactor = 1; }

        if (m_nonLinearScaleFactor <= 0)
        { m_nonLinearScaleFactor = 0; }
        else if (m_nonLinearScaleFactor >= 1)
        { m_nonLinearScaleFactor = 1; }

        // update changed parameters
        m_prev_scaleCenter = m_scaleCenter;
        m_prev_linearScaleArea = m_linearScaleArea;
        m_prev_linearScaleFactor = m_linearScaleFactor;
        m_prev_nonLinearScaleFactor = m_nonLinearScaleFactor;

        // transform to internal parameters
        m_intern_scaleCenter = m_scaleCenter;
        m_intern_linearScaleArea = m_linearScaleArea;
        m_intern_linearScaleFactor = m_linearScaleFactor;
        m_intern_nonLinearScaleFactor = m_nonLinearScaleFactor*0.4-0.2;

        // calculate borders based on parameters
        m_borderXAbsLeftSrc = int(m_intern_scaleCenter * m_width - m_intern_linearScaleArea / 2 * m_width);
        m_borderXAbsLeftDst = int(m_intern_scaleCenter * m_width - m_intern_linearScaleArea / 2 * m_width * m_intern_linearScaleFactor);

        m_borderXAbsRightSrc = int(m_intern_scaleCenter * m_width + m_intern_linearScaleArea / 2 * m_width);
        m_borderXAbsRightDst = int(m_intern_scaleCenter * m_width + m_intern_linearScaleArea / 2 * m_width * m_intern_linearScaleFactor);

        // validate if resulting borders are still within the valid range for later calculation [1,m_width-1]
        if (m_borderXAbsLeftSrc <= 1)
        { m_borderXAbsLeftSrc = 1; }
        else if (m_borderXAbsLeftSrc >= m_width-1)
        { m_borderXAbsLeftSrc = m_width-1; }

        if (m_borderXAbsRightSrc <= 1)
        { m_borderXAbsRightSrc = 1; }
        else if (m_borderXAbsRightSrc >= m_width-1)
        { m_borderXAbsRightSrc = m_width-1; }

        if (m_borderXAbsLeftDst <= 1)
        { m_borderXAbsLeftDst = 1; }
        else if (m_borderXAbsLeftDst >= m_width-1)
        { m_borderXAbsLeftDst = m_width-1; }

        if (m_borderXAbsRightDst <= 1)
        { m_borderXAbsRightDst = 1; }
        else if (m_borderXAbsRightDst >= m_width-1)
        { m_borderXAbsRightDst = m_width-1; }

        
    }

    void calcTransformationFactors()
    {
        if (m_transformationCalculations == NULL)
        {
            m_transformationCalculations = new TransformationElem[m_width];
        }

        for (int colIdx=0; colIdx<m_width; colIdx++)
        {
            double relativeSrcXPos = 0;
            unsigned int higherXPos, lowerXPos;
            double lowerWeight = 0;
            double higherWeight = 0;

            unsigned int offsetSrcX = 0;
            unsigned int offsetDstX = 0;
            unsigned int lengthSrcSection = m_borderXAbsLeftSrc - offsetSrcX - 1;
            unsigned int lengthDstSection = m_borderXAbsLeftDst - offsetDstX - 1;

            // y=SIN(x*pi-pi)*a+x
            double linearRatio = (double)(colIdx-offsetDstX)/lengthDstSection;
            double ratio = sin(linearRatio*PI-PI)*m_intern_nonLinearScaleFactor+linearRatio;

            if (colIdx > m_borderXAbsLeftDst)
            {
                offsetSrcX = m_borderXAbsLeftSrc;
                offsetDstX = m_borderXAbsLeftDst;
                lengthSrcSection = m_borderXAbsRightSrc - offsetSrcX - 1;
                lengthDstSection = m_borderXAbsRightDst - offsetDstX - 1;
                // linear section
                double linearRatio = (double)(colIdx-offsetDstX)/lengthDstSection;
                ratio = ((double)(colIdx-offsetDstX)/lengthDstSection);
            }
                
            if (colIdx > m_borderXAbsRightDst)
            {
                offsetSrcX = m_borderXAbsRightSrc;
                offsetDstX = m_borderXAbsRightDst;
                lengthSrcSection = m_width - offsetSrcX - 1;
                lengthDstSection = m_width - offsetDstX - 1;

                // y=SIN(x*pi)*a+x
                double linearRatio = (double)(colIdx-offsetDstX)/lengthDstSection;
                ratio = sin(linearRatio*PI)*m_intern_nonLinearScaleFactor+linearRatio;
            }

            relativeSrcXPos = ratio * lengthSrcSection;
            
            lowerXPos = (int)floor(relativeSrcXPos);
            higherXPos = (int)ceil(relativeSrcXPos);
            if (higherXPos >= lengthSrcSection)
            {
                higherXPos = lengthSrcSection;
            }
            if (lowerXPos >= lengthSrcSection)
            {
                lowerXPos = lengthSrcSection;
            }
            
            if (higherXPos == lowerXPos)
            {
                lowerWeight = higherWeight = 0.5;
            } else {
                lowerWeight = relativeSrcXPos - (double)lowerXPos;
                higherWeight = (double)higherXPos - relativeSrcXPos;
            }

            m_transformationCalculations[colIdx].higherXPos = higherXPos + offsetSrcX;
            m_transformationCalculations[colIdx].lowerXPos = lowerXPos + offsetSrcX;
            m_transformationCalculations[colIdx].higherWeight = higherWeight;
            m_transformationCalculations[colIdx].lowerWeight = lowerWeight;
        }
    }


};

frei0r::construct<ElasticScale> plugin("Elastic scale filter",
                "This is a frei0r filter which allows to scale video footage non-linearly.",
                "Matthias Schnoell",
                0,2,
                F0R_COLOR_MODEL_RGBA8888);

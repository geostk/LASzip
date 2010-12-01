/******************************************************************************
 *
 * Project:  laszip - http://liblas.org -
 * Purpose:
 * Author:   Martin Isenburg
 *           isenburg at cs.unc.edu
 *
 ******************************************************************************
 * Copyright (c) 2009, Martin Isenburg
 *
 * This is free software; you can redistribute and/or modify it under
 * the terms of the GNU Lesser General Licence as published
 * by the Free Software Foundation.
 *
 * See the COPYING file for more information.
 *
 ****************************************************************************/

/*
===============================================================================

  FILE:  laspointwriter1compressedarithmetic.cpp
  
  CONTENTS:
  
    see corresponding header file
  
  PROGRAMMERS:
  
    martin isenburg@cs.unc.edu
  
  COPYRIGHT:
  
    copyright (C) 2007  martin isenburg@cs.unc.edu
    
    This software is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  
  CHANGE HISTORY:
  
    see corresponding header file
  
===============================================================================
*/

#include <math.h>
#include "laspointwriter1compressedarithmetic.hpp"

#define MULTI_MAX 512

bool LASpointWriter1compressedArithmetic::write_point(LASpoint* point, double gps_time, unsigned short* rgb)
{
	if (enc)
	{
    // find the median difference among the three preceding differences
    int median_x;
    if (last_x_diff[last_dir][0] < last_x_diff[last_dir][1])
    {
      if (last_x_diff[last_dir][1] < last_x_diff[last_dir][2])
        median_x = last_x_diff[last_dir][1];
      else if (last_x_diff[last_dir][0] < last_x_diff[last_dir][2])
        median_x = last_x_diff[last_dir][2];
      else
        median_x = last_x_diff[last_dir][0];
    }
    else
    {
      if (last_x_diff[last_dir][0] < last_x_diff[last_dir][2])
        median_x = last_x_diff[last_dir][0];
      else if (last_x_diff[last_dir][1] < last_x_diff[last_dir][2])
        median_x = last_x_diff[last_dir][2];
      else
        median_x = last_x_diff[last_dir][1];
    }

    int median_y;
    if (last_y_diff[last_dir][0] < last_y_diff[last_dir][1])
    {
      if (last_y_diff[last_dir][1] < last_y_diff[last_dir][2])
        median_y = last_y_diff[last_dir][1];
      else if (last_y_diff[last_dir][0] < last_y_diff[last_dir][2])
        median_y = last_y_diff[last_dir][2];
      else
        median_y = last_y_diff[last_dir][0];
    }
    else
    {
      if (last_y_diff[last_dir][0] < last_y_diff[last_dir][2])
        median_y = last_y_diff[last_dir][0];
      else if (last_y_diff[last_dir][1] < last_y_diff[last_dir][2])
        median_y = last_y_diff[last_dir][2];
      else
        median_y = last_y_diff[last_dir][1];
    }

    // compress x y z coordinates
    int x_diff = point->x - last_point.x;
    int y_diff = point->y - last_point.y;

    ic_dx->Compress(median_x, x_diff, last_dir);
    // we use the number k of bits corrector bits to switch contexts
    int k_bits = ic_dx->GetK();
    ic_dy->Compress(median_y, y_diff, k_bits);
    k_bits = (ic_dx->GetK() + ic_dy->GetK()) / 2;
    ic_z->Compress(last_point.z, point->z, k_bits);

		// compress which other values have changed
		int changed_values = ((last_gps_time.f64 != gps_time) << 6) |
												 ((last_point.intensity != point->intensity) << 5) |
												 ((((unsigned char*)&last_point)[14] != ((unsigned char*)point)[14]) << 4) |
												 ((last_point.classification != point->classification) << 3) |
												 ((last_point.scan_angle_rank != point->scan_angle_rank) << 2) |
												 ((last_point.user_data != point->user_data) << 1) |
												 ((last_point.point_source_ID != point->point_source_ID));

		enc->encode(m_changed_values, changed_values);

		// compress the intensity if it has changed
		if (changed_values & 32)
		{
			ic_intensity->Compress(last_point.intensity, point->intensity);
		}

		// compress the edge_of_flight_line, scan_direction_flag, ... if it has changed
		if (changed_values & 16)
		{
			enc->encode(m_bit_byte, ((unsigned char*)point)[14]);
		}

		// compress the classification ... if it has changed
		if (changed_values & 8)
		{
			enc->encode(m_classification, point->classification);
		}
		
		// compress the scan_angle_rank ... if it has changed
		if (changed_values & 4)
		{
			ic_scan_angle_rank->Compress(last_point.scan_angle_rank, point->scan_angle_rank, k_bits < 3);
		}

		// compress the user_data ... if it has changed
		if (changed_values & 2)
		{
			enc->encode(m_user_data, point->user_data);
		}

		// compress the point_source_ID ... if it has changed
		if (changed_values & 1)
		{
			ic_point_source_ID->Compress(last_point.point_source_ID, point->point_source_ID);
		}

		// compress the gps_time ... if it has changed
    if (changed_values & 64)
    {
      I64F64 this_gps_time;
      this_gps_time.f64 = gps_time;
      I64 gps_time_diff = this_gps_time.i64 - last_gps_time.i64;

      if (-1<<30 < gps_time_diff && gps_time_diff < 1<<30)
      {
        int multi = 1;
        if (last_gps_time_diff)
        {
          multi = (int)(((float)gps_time_diff / (float)last_gps_time_diff) + 0.5);
          if (multi >= MULTI_MAX-2)
          {
            multi = MULTI_MAX-2;
      	    multi_extreme_counter++;
          }
          else if (multi <= 0)
          {
            multi = 0;
      	    multi_extreme_counter++;
          }
        }
        enc->encode(m_gps_time_multi[k_bits < 3], multi);
	      if (multi == 1)
        {
	        ic_gps_time->Compress(last_gps_time_diff, (I32)gps_time_diff, 0);
          last_gps_time_diff = (I32)gps_time_diff;
          multi_extreme_counter = 0;	  
	      }
	      else if (multi == 0)
        {
          ic_gps_time->Compress(last_gps_time_diff/2, (I32)gps_time_diff, 3);
	        if (multi_extreme_counter > 3)
	        {
	          last_gps_time_diff = (I32)gps_time_diff;
	          multi_extreme_counter = 0;
	        }
	      }
	      else if (multi < 10)
	      {
	        ic_gps_time->Compress(multi*last_gps_time_diff, (I32)gps_time_diff, 1);
	      }
	      else
        {
	        ic_gps_time->Compress(multi*last_gps_time_diff, (I32)gps_time_diff, 2);
	        if (multi_extreme_counter > 3)
	        {
	          last_gps_time_diff = (I32)gps_time_diff;
	          multi_extreme_counter = 0;
	        }
	      }
      }
      else
      {
        enc->encode(m_gps_time_multi[k_bits < 3], MULTI_MAX-1);
        enc->writeDouble(gps_time);
      }
    }

    // only record the difference if the scan direction has not changed
    if (last_dir == point->scan_direction_flag)
    {
      last_x_diff[last_dir][last_incr[last_dir]] = x_diff;
      last_y_diff[last_dir][last_incr[last_dir]] = y_diff;

      last_incr[last_dir]++;
      if (last_incr[last_dir] > 2) last_incr[last_dir] = 0;
    }
  }
	else
	{
		fwrite(point, sizeof(LASpoint), 1, file);
		fwrite(&(gps_time), sizeof(double), 1, file);
		init_encoder();
  }

  last_dir = point->scan_direction_flag;
	last_point = *point;
	last_gps_time.f64 = gps_time;

	return true;
}

LASpointWriter1compressedArithmetic::LASpointWriter1compressedArithmetic(FILE* file)
{
	this->file = file;
  last_dir = 0;
	last_x_diff[0][0] = last_x_diff[0][1] = last_x_diff[0][2] = last_x_diff[1][0] = last_x_diff[1][1] = last_x_diff[1][2] = 0;
	last_y_diff[0][0] = last_y_diff[0][1] = last_y_diff[0][2] = last_y_diff[1][0] = last_y_diff[1][1] = last_y_diff[1][2] = 0;
  last_incr[0] = last_incr[1] = 0;
	last_gps_time.f64 = 0;
	last_gps_time_diff = 0;
	enc = 0;
}

void LASpointWriter1compressedArithmetic::init_encoder()
{
	enc = new ArithmeticEncoder(file,false);

	ic_dx = new ArithmeticIntegerCompressor();
	ic_dy = new ArithmeticIntegerCompressor();
	ic_z = new ArithmeticIntegerCompressor();

	ic_dx->SetPrecision(32); 
	ic_dy->SetPrecision(32); 
	ic_z->SetPrecision(32); 

  ic_dx->SetupCompressor(enc, 2);
  ic_dy->SetupCompressor(enc, 33);
  ic_z->SetupCompressor(enc, 33);

	m_changed_values = new ArithmeticModel(128,0,1);

	ic_intensity = new ArithmeticIntegerCompressor();
	ic_intensity->SetPrecision(16);
  ic_intensity->SetupCompressor(enc);

	m_bit_byte = new ArithmeticModel(256,0,1);
	m_classification = new ArithmeticModel(256,0,1);

	ic_scan_angle_rank = new ArithmeticIntegerCompressor();
	ic_scan_angle_rank->SetPrecision(8);
  ic_scan_angle_rank->SetupCompressor(enc, 2);

	m_user_data = new ArithmeticModel(256,0,1);

	ic_point_source_ID = new ArithmeticIntegerCompressor();
	ic_point_source_ID->SetPrecision(16);
  ic_point_source_ID->SetupCompressor(enc);

  m_gps_time_multi = new ArithmeticModel*[2];
  for (int i = 0; i < 2; i++) m_gps_time_multi[i] = new ArithmeticModel(MULTI_MAX,0,1);

	ic_gps_time = new ArithmeticIntegerCompressor();
	ic_gps_time->SetPrecision(32);
  ic_gps_time->SetupCompressor(enc, 4);

	multi_extreme_counter = 0;
}

LASpointWriter1compressedArithmetic::~LASpointWriter1compressedArithmetic()
{
	if (enc)
	{
		enc->done();

		delete enc;
		delete ic_dx;
		delete ic_dy;
		delete ic_z;
		delete m_changed_values;
		delete ic_intensity;
		delete m_bit_byte;
		delete m_classification;
		delete ic_scan_angle_rank;
		delete m_user_data;
		delete ic_point_source_ID;
    for (int i = 0; i < 2; i++) delete m_gps_time_multi[i];
		delete [] m_gps_time_multi;
		delete ic_gps_time;
	}
}

TriLED Tracker
##############

Simple module to determine the position and orientation of an object or subject by
a triangle of red/green/blue LEDs.

.. warning::
    This module was useful in the past, but better methods and sensors exist now for
    tracking. It has not been updated for a while.


Usage
=====

No configuration parameters are currently available. Connect a frame source
and read resulting data.


Ports
=====

.. list-table::
   :widths: 14 10 22 54
   :header-rows: 1

   * - Name
     - Direction
     - Data Type
     - Description

   * - 🠺Frames
     - In
     - ``Frame``
     - Input frames to analyze.
   * - Animal Visualization🠺
     - Out
     - ``Frame``
     - Visualization of the subject orientation.
   * - Tracking Visualization🠺
     - Out
     - ``Frame``
     - Visualization of the subject tracking.
   * - Tracking Data🠺
     - Out
     - ``TableRow``
     - Position data as table.


Stream Metadata
===============

.. list-table::
   :widths: 15 85
   :header-rows: 1

   * - Name
     - Metadata

   * - Tracking Data🠺
     - | ``table_header``: String List, Table header
   * - Animal Visualization🠺
     - | ``framerate``: Double, frame rate in FPS.
   * - Tracking Visualization🠺
     - | ``framerate``: Double, frame rate in FPS.
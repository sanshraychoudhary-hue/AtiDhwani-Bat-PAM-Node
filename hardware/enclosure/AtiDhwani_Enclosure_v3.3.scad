// ==============================================================================
// AtiDhwani Ultrasonic Bat PAM Node & Detector — Parametric 3D Enclosure
// Revision: v3.3 Compact (Fits 98.0 mm x 98.0 mm Motherboard + Dual 18650 Pack)
// License: CERN-OHL-P / MIT Open Hardware
// ==============================================================================
// Set which part to render:
// "assembly"  : Complete assembled view
// "exploded"  : Exploded presentation view
// "base"      : Bottom base shell with dual 18650 cradle & standoffs (Ready for 3D printing)
// "lid"       : Top lid with acoustic horns, OLED window, and button holes (Ready for 3D printing)
part = "assembly"; // [assembly, exploded, base, lid]

// ------------------------------------------------------------------------------
// Parametric Global Dimensions (mm)
// ------------------------------------------------------------------------------
$fn = 60; // Circle smoothness

pcb_w         = 98.0;   // PCB X dimension
pcb_l         = 98.0;   // PCB Y dimension
pcb_thick     = 1.6;    // Standard FR-4 thickness
pcb_clearance = 1.5;    // Internal gap around PCB perimeter

wall_thick    = 2.5;    // Enclosure wall thickness
base_floor    = 2.5;    // Bottom plate thickness
lid_roof      = 2.5;    // Top plate thickness

// Internal cavity: 101.0 x 101.0 mm
inner_w       = pcb_w + (pcb_clearance * 2);
inner_l       = pcb_l + (pcb_clearance * 2);

// Outer shell dimensions: 106.0 x 106.0 mm
outer_w       = inner_w + (wall_thick * 2);
outer_l       = inner_l + (wall_thick * 2);

// Heights
batt_height   = 19.5;   // 18650 diameter with cradle
standoff_h    = 22.0;   // PCB floor standoff height (clears batteries + through-hole pins)
base_height   = standoff_h + pcb_thick + 2.0; // 25.6 mm
lid_interior_h= 15.0;   // Clears tall ESP32 & AD828 modules
lid_height    = lid_interior_h + lid_roof;    // 17.5 mm
lip_h         = 2.5;    // Interlocking dust/water rim height

// PCB Mounting Holes Pattern (Centered at 0,0)
hole_spacing_x = 88.0;  // 193 - 105 mm
hole_spacing_y = 88.0;  // 193 - 105 mm

// ------------------------------------------------------------------------------
// Component Port Offsets (relative to PCB center at 0,0)
// ------------------------------------------------------------------------------
// Top Edge: Ultrasonic Acoustic Horns (Knowles & Infineon MEMS)
mic_knowles_x  = -36.0;
mic_knowles_y  = -44.0;
mic_infineon_x = -23.0;
mic_infineon_y = -44.0;

// Left Edge: Power Management
usb_c_y        = 0.0;    // TP4056 USB-C port centered along left wall
toggle_sw_y    = 15.0;   // Panel mount toggle switch location

// Right Edge: Outward Peripherals
sd_card_y      = -25.0;  // MicroSD push-push slot
headphone_y    = 26.5;   // 3.5mm stereo DAC headphone jack

// Top Lid: Controls & Display
oled_x         = 29.0;
oled_y         = -38.0;
encoder_x      = 35.0;
encoder_y      = 36.0;

// 4x Tactile Buttons along Y = 37.0
btn_y          = 37.0;
btn_rec_x      = -10.0;
btn_disp_x     = 1.0;
btn_up_x       = 12.0;
btn_down_x     = 23.0;

// Pro-Range INR 18650 3.7V 5000mAh 1S2P Li-Ion Battery Pack (Robu SKU: R193689)
// Pre-assembled shrink-wrapped dual-cell block with internal protection board
batt_pocket_w = 41.0;   // 38.5mm pack width + 2.5mm clearance
batt_pocket_l = 72.0;   // 68.5mm pack length + 3.5mm wire clearance
batt_wall_h   = 8.0;    // 8mm perimeter retaining wall
batt_wall_t   = 2.0;    // 2mm cradle wall thickness

// ==============================================================================
// Module Definitions
// ==============================================================================

module rounded_box(x, y, z, r) {
    translate([r - x/2, r - y/2, 0])
        minkowski() {
            cube([x - 2*r, y - 2*r, z - 1]);
            cylinder(r=r, h=1);
        }
}

// ------------------------------------------------------------------------------
// Bottom Base Shell
// ------------------------------------------------------------------------------
module base_shell() {
    difference() {
        union() {
            // Main solid outer box with 4mm rounded corners
            rounded_box(outer_w, outer_l, base_height, 4.0);
            
            // PCB Mounting Standoffs (4x M3)
            for (hx = [-hole_spacing_x/2, hole_spacing_x/2]) {
                for (hy = [-hole_spacing_y/2, hole_spacing_y/2]) {
                    translate([hx, hy, base_floor])
                        cylinder(d=7.0, h=standoff_h);
                }
            }
            
            // Lid Screw Bosses (4 corners)
            for (cx = [-outer_w/2 + 5.5, outer_w/2 - 5.5]) {
                for (cy = [-outer_l/2 + 5.5, outer_l/2 - 5.5]) {
                    translate([cx, cy, base_floor])
                        cylinder(d=8.0, h=base_height - base_floor);
                }
            }
            
            // Pro-Range 1S2P Battery Pack Retention Cradle
            translate([0, 0, base_floor])
                difference() {
                    rounded_box(batt_pocket_w + 2*batt_wall_t, batt_pocket_l + 2*batt_wall_t, batt_wall_h, 3.0);
                    rounded_box(batt_pocket_w, batt_pocket_l, batt_wall_h + 2, 2.0);
                    // Wire exit notch toward toggle switch & J_BATT
                    translate([-batt_pocket_w/2, 15.0, batt_wall_h/2])
                        cube([12.0, 16.0, batt_wall_h + 2], center=true);
                    // Retention strap slots
                    translate([0, -15.0, 1.5]) cube([batt_pocket_w + 10.0, 5.0, 3.0], center=true);
                    translate([0, 15.0, 1.5]) cube([batt_pocket_w + 10.0, 5.0, 3.0], center=true);
                }
        }
        
        // Hollow interior cavity
        translate([0, 0, base_floor])
            rounded_box(inner_w, inner_l, base_height, 2.0);
            
        // Interlocking Lip Step on Base Rim
        translate([0, 0, base_height - lip_h])
            rounded_box(inner_w + 1.2, inner_l + 1.2, lip_h + 1, 2.0);
            
        // 4x PCB Standoff Pilot Holes (2.8mm for M3 self-tapping / threaded insert)
        for (hx = [-hole_spacing_x/2, hole_spacing_x/2]) {
            for (hy = [-hole_spacing_y/2, hole_spacing_y/2]) {
                translate([hx, hy, base_floor + standoff_h - 8.0])
                    cylinder(d=2.8, h=10.0);
            }
        }
        
        // 4x Corner Lid Screw Pilot Holes (3.0mm for M3 heat-set brass insert)
        for (cx = [-outer_w/2 + 5.5, outer_w/2 - 5.5]) {
            for (cy = [-outer_l/2 + 5.5, outer_l/2 - 5.5]) {
                translate([cx, cy, base_height - 10.0])
                    cylinder(d=4.0, h=12.0);
            }
        }
        
        // LEFT WALL: Toggle Switch Port (6.2mm dia for MTS-102 mini toggle)
        translate([-outer_w/2 - 1, toggle_sw_y, base_floor + 10.0])
            rotate([0, 90, 0])
                cylinder(d=6.2, h=wall_thick + 4);
                
        // LEFT WALL: TP4056 USB-C Charging Port Slot
        translate([-outer_w/2 - 1, usb_c_y - 5.0, base_floor + standoff_h - 1.5])
            cube([wall_thick + 4, 10.0, 4.5]);
            
        // RIGHT WALL: MicroSD Push-Push Slot
        translate([outer_w/2 - wall_thick - 1, sd_card_y - 7.5, base_floor + standoff_h - 1.5])
            cube([wall_thick + 4, 15.0, 3.5]);
            
        // RIGHT WALL: 3.5mm Headphone Jack Port
        translate([outer_w/2 - wall_thick - 1, headphone_y, base_floor + standoff_h + 1.0])
            rotate([0, 90, 0])
                cylinder(d=7.5, h=wall_thick + 4);
                
        // BOTTOM EXTERIOR: 1/4"-20 Camera Tripod Nut Trap (Standard PAM tree/tripod mount)
        translate([0, 0, -1])
            cylinder(d=8.0, h=base_floor + 2);
    }
}

// ------------------------------------------------------------------------------
// Top Lid Shell
// ------------------------------------------------------------------------------
module lid_shell() {
    difference() {
        union() {
            // Outer Lid Box
            rounded_box(outer_w, outer_l, lid_height, 4.0);
            
            // Interlocking Male Lip (Snugs into base rim)
            translate([0, 0, -lip_h])
                rounded_box(inner_w + 0.8, inner_l + 0.8, lip_h + 0.1, 2.0);
        }
        
        // Hollow Internal Ceiling Cavity
        translate([0, 0, -lip_h - 1])
            rounded_box(inner_w, inner_l, lid_interior_h + lip_h + 1, 2.0);
            
        // ACOUSTIC APERTURES: Conical Ultrasonic Horns
        // 1. Knowles SPV0142 Acoustic Horn
        translate([mic_knowles_x, mic_knowles_y, -1]) {
            cylinder(d1=3.5, d2=6.5, h=lid_height + 2);
            // Underside gasket / mesh recess
            translate([0, 0, lid_roof + 0.2])
                cylinder(d=10.0, h=1.5);
        }
        
        // 2. Infineon IM70A Acoustic Horn
        translate([mic_infineon_x, mic_infineon_y, -1]) {
            cylinder(d1=3.5, d2=6.5, h=lid_height + 2);
            // Underside gasket / mesh recess
            translate([0, 0, lid_roof + 0.2])
                cylinder(d=10.0, h=1.5);
        }
        
        // OLED DISPLAY WINDOW (32 x 13 mm)
        translate([oled_x - 16.0, oled_y - 6.5, -1])
            cube([32.0, 13.0, lid_height + 2]);
            
        // EC11 ROTARY ENCODER TUNING SHAFT (8.5mm dia)
        translate([encoder_x, encoder_y, -1])
            cylinder(d=8.5, h=lid_height + 2);
            
        // 4x TACTILE BUTTON PORTS (5.0mm dia)
        for (bx = [btn_rec_x, btn_disp_x, btn_up_x, btn_down_x]) {
            translate([bx, btn_y, -1])
                cylinder(d=5.0, h=lid_height + 2);
        }
        
        // 4x CORNER COUNTERBORE SCREW HOLES (M3 Socket Head)
        for (cx = [-outer_w/2 + 5.5, outer_w/2 - 5.5]) {
            for (cy = [-outer_l/2 + 5.5, outer_l/2 - 5.5]) {
                translate([cx, cy, -lip_h - 2]) {
                    cylinder(d=3.4, h=lid_height + 10);
                    translate([0, 0, lid_height + lip_h - 2.5])
                        cylinder(d=6.2, h=5.0); // Socket head recess
                }
            }
        }
    }
}

// ------------------------------------------------------------------------------
// Visual PCB & Component Dummy (For Assembly Views)
// ------------------------------------------------------------------------------
module pcb_dummy() {
    color([0.1, 0.5, 0.2, 0.8])
        translate([0, 0, base_floor + standoff_h])
            cube([pcb_w, pcb_l, pcb_thick], center=true);
            
    // 18650 Battery Dummy (2 cells in parallel)
    color([0.2, 0.4, 0.9, 0.9]) {
        translate([-10.0, 0, base_floor + cell_dia/2 + 1.0])
            rotate([90, 0, 0])
                cylinder(d=18.0, h=65.0, center=true);
        translate([10.0, 0, base_floor + cell_dia/2 + 1.0])
            rotate([90, 0, 0])
                cylinder(d=18.0, h=65.0, center=true);
    }
}

// ==============================================================================
// Render Selector
// ==============================================================================
if (part == "assembly") {
    color([0.3, 0.35, 0.4, 0.7]) base_shell();
    pcb_dummy();
    color([0.2, 0.25, 0.3, 0.6]) 
        translate([0, 0, base_height + lip_h + 1.0])
            lid_shell();
}
else if (part == "exploded") {
    color([0.3, 0.35, 0.4]) base_shell();
    pcb_dummy();
    color([0.2, 0.25, 0.3]) 
        translate([0, 0, base_height + 40.0])
            lid_shell();
}
else if (part == "base") {
    base_shell();
}
else if (part == "lid") {
    // Flip lid 180 degrees so top surface is flat on print bed
    rotate([180, 0, 0])
        lid_shell();
}

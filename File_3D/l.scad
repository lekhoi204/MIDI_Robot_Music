// U Bracket - Corrected version for Tinkercad / OpenSCAD 
// Dimensions in mm

difference() {
    // Main solid U-shape
    union() {
       
        // Left flange
        cube([54.2, 20, 2], center = false);
        translate([54.2, 0, 0])
            cube([2, 20, 104.2], center = false);
        translate([20, 0, 104.2])
        cube([36.2, 20, 2], center = false);
        translate([18, 0, 56.2])
            cube([2, 20, 50], center = false);
        
    }

    // Holes for M4 bolts (Ø4.5 mm)
    translate([10, 10, 0])
        cylinder(h = 4, r = 2.25, center = true, $fn = 60);}
    
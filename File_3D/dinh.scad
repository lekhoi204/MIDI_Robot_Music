$fn = 64; // độ mịn bề mặt

// Hình trụ chính dài 32mm, đường kính 3.5mm
cylinder(h = 32, d = 3.5);

// Đầu loe ra (2mm) — nón cụt đường kính 3.5 -> 5mm
translate([0, 0, 32])
cylinder(h = 3, d1 = 6.5, d2 = 6.5);

# Do not modify 

joint: CURR_BLADE + MAST_C, revolute hinge,
    CURR_BLADE + COMPASS_L,
        position, reference, node, -COMPASS_SPHERICAL_RADIAL_OFFSET, -COMPASS_REVOLUTE_VERTICAL_OFFSET, 0.0,
        orientation, reference, node, eye,
    MAST,
        position, reference, other node,  -COMPASS_SPHERICAL_RADIAL_OFFSET, -COMPASS_REVOLUTE_VERTICAL_OFFSET, 0.0,
        orientation, reference,other node, eye;


joint: CURR_BLADE + HUB_C, revolute hinge,
    CURR_BLADE + COMPASS_U,
        position, reference, node, -COMPASS_SPHERICAL_RADIAL_OFFSET, COMPASS_REVOLUTE_VERTICAL_OFFSET, 0.0,
        orientation, reference, node, eye,
    HUB,
        position, reference, other node,  -COMPASS_SPHERICAL_RADIAL_OFFSET, COMPASS_REVOLUTE_VERTICAL_OFFSET, 0.0,
        orientation, reference,other node, eye;

joint: CURR_BLADE + COMPASS_SPH, spherical hinge,
    CURR_BLADE + COMPASS_U,
        position, reference, node, null,
        orientation, reference, node, eye,
    CURR_BLADE + COMPASS_L,
        position, reference, node, null,
        orientation, reference, node, eye;

joint regularization: CURR_BLADE + COMPASS_SPH, tikhonov, REGULARIZATION_COMPLIANCE;

# vim:ft=mbd

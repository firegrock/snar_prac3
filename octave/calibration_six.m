function [bias, scale] = calibration_six(means, g0)
  % means: six rows +X,-X,+Y,-Y,+Z,-Z; columns XYZ in nominal m/s^2, S frame.
  if nargin<2, g0=9.80665; endif
  if !isequal(size(means),[6,3]) || any(!isfinite(means(:))), error('Invalid six means'); endif
  for face=1:6
    ax=ceil(face/2); sign=1-2*mod(face+1,2);
    if sign*means(face,ax)<0.75*g0 || any(abs(means(face,setdiff(1:3,ax)))>0.3*g0), error('Wrong face/sign'); endif
  endfor
  plus=[means(1,1),means(3,2),means(5,3)]; minus=[means(2,1),means(4,2),means(6,3)];
  bias=(plus+minus)/2; scale=2*g0./(plus-minus);
  if any(scale<0.5 | scale>1.5) || any(abs(bias)>5), error('Implausible calibration'); endif
endfunction

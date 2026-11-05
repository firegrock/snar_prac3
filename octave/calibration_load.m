function coefficients = calibration_load(path)
  coefficients = dlmread(path, ',', 1, 0);
  if numel(coefficients)!=10 || any(!isfinite(coefficients)) || any(coefficients(7:9)<=0)
    error('Expected gbx,gby,gbz,abx,aby,abz,asx,asy,asz,temperature_c');
  endif
endfunction

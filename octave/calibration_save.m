function calibration_save(path, coefficients)
  if numel(coefficients)!=10 || any(!isfinite(coefficients)) || any(coefficients(7:9)<=0), error('Invalid coefficients'); endif
  if exist(path,'file'), error('Refusing to overwrite existing file'); endif
  f=fopen(path,'w'); if f<0, error('Cannot create file'); endif
  fprintf(f,'gbx,gby,gbz,abx,aby,abz,asx,asy,asz,temperature_c\n');
  fprintf(f,'%.12g,%.12g,%.12g,%.12g,%.12g,%.12g,%.12g,%.12g,%.12g,%.12g\n',coefficients);
  fclose(f);
endfunction

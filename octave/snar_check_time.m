function segments = snar_check_time(run)
  s = run.samples; rate = run.meta(1);
  bad = [true; diff(s(:,1)) != 1 | diff(s(:,2)) <= 0 | diff(s(:,2)) > 2.5e6/rate];
  bad = bad | bitand(uint32(s(:,14)), uint32(16)) != 0;
  segments = cumsum(bad);
  if any(bad(2:end))
    warning('Detected gaps: split integration into segments; no interpolation/zero fill');
  endif
endfunction

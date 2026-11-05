function test_fixture(path)
  data=dlmread(path,',',1,0); q=[1,0,0,0]; integral=[0,0,0]; max_q=0; max_i=0; prev=0;
  for k=1:rows(data)
    [q,integral,ok]=mahony_step(q,integral,data(k,2:4),data(k,5:7),2,0.05,(data(k,1)-prev)*1e-6);
    assert(ok); prev=data(k,1); qc=data(k,8:11);
    max_q=max(max_q,min(norm(q-qc),norm(q+qc))); max_i=max(max_i,norm(integral-data(k,12:14)));
  endfor
  % C float32 vs Octave float64, 1500 Euler steps/15s: implementation tolerance,
  % NOT physical sensor error. Sign-equivalent quaternions are accepted.
  assert(max_q<2e-5); assert(max_i<1e-5);
  fprintf('PASS C/Octave fixture: max quaternion norm error %.9g, integral %.9g\n',max_q,max_i);
endfunction

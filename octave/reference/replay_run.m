function result = replay_run(run)
  m=run.meta; s=run.samples; boundary=m(33); idx=find(s(:,1)==boundary,1);
  if isempty(idx), error('Initial boundary sample unavailable: cannot claim exact replay'); endif
  q=m(26:29); integral=m(30:32); previous=m(34); ready=logical(m(35)); still_n=m(36); mean_a=m(37:39); m2=m(40:42);
  result=NaN(rows(s),4); max_error=0; compared=0;
  for k=idx:rows(s)
    if k>idx && s(k,1)!=s(k-1,1)+1, error('Missing raw sample: exact replay refused'); endif
    t=s(k,2); dt=(t-previous)*1e-6;
    gap=(previous!=0 && (dt<=0 || dt>2.5/m(1)));
    % TIME_GAP can also designate a FIFO resync without a long time interval.
    if gap || bitand(uint32(s(k,14)),uint32(16))
      q=[1,0,0,0]; integral=[0,0,0]; ready=false; still_n=0; mean_a=[0,0,0]; m2=[0,0,0]; previous=0;
    endif
    had_time=(previous!=0); previous=t;
    a=run.f_B(k,:); w=run.omega_B(k,:); na=norm(a);
    if !ready
      if all(isfinite(a)) && all(isfinite(w)) && na>1e-6 && abs(na-m(4))<=0.2*m(4) && norm(w)<0.08
        still_n+=1; d=a-mean_a; mean_a+=d/still_n; m2+=d.*(a-mean_a);
        if still_n>=m(1)
          if all(m2/(still_n-1)<=0.01)
            r=atan2(mean_a(2),mean_a(3))/2; p=atan2(-mean_a(1),hypot(mean_a(2),mean_a(3)))/2;
            q=[cos(r)*cos(p),sin(r)*cos(p),cos(r)*sin(p),-sin(r)*sin(p)]; ready=true;
          else still_n=0; mean_a=[0,0,0]; m2=[0,0,0]; endif
        endif
      else still_n=0; mean_a=[0,0,0]; m2=[0,0,0]; endif
    elseif had_time
      [q,integral,ok]=mahony_step(q,integral,a,w,m(5),m(6),dt);
      if !ok, error('Invalid Mahony interval/data'); endif
    endif
    result(k,:)=q;
    if ready && !bitand(uint32(s(k,14)),uint32(1))
      qc=s(k,10:13); max_error=max(max_error,min(norm(q-qc),norm(q+qc))); compared+=1;
    endif
  endfor
  fprintf('Compared %d valid states, max q/-q norm error %.9g. Check float accumulation over run length.\n',compared,max_error);
endfunction
